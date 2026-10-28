#include "../gfx/nvn/nvn_mtl.h"
#include "../gfx/nvn/nvn_obj.h"
#include "../gfx/tex/nvn_deswizzle.h"
#include "../host/front.h"
#include "../host/dm.h"
#include "../host/hle.h"
#include "../host/hle_movie_avc.h"
#include "vk_win.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

extern void front_win_present_bgra(const uint32_t *bgra, int w, int h, uint64_t frame, uint64_t submits, uint64_t draws, uint64_t clears, uint32_t last_clear_bgra);
extern int front_win_capture_bmp(const char *path);

static _Atomic uint64_t g_fence_cnt = 0;
static _Atomic uint64_t g_cur_frame = 0;
static _Atomic uint64_t g_stat_pass = 0;
static _Atomic uint64_t g_stat_draw = 0;
static _Atomic uint64_t g_stat_clear = 0;
static _Atomic uint64_t g_stat_submit = 0;
static _Atomic uint64_t g_present_n = 0;
static _Atomic uint32_t g_last_clear_bgra = 0xFF1A1E24u;

static p_set_rt g_rt;
static int g_has_rt;
static const tex_t *g_last_rendered_rt;
static const tex_t *g_main_scene_rt;

static inline int is_main_rt(void) {
    if (!g_has_rt || g_rt.n <= 0 || !g_rt.color[0]) return 0;
    uint32_t w = g_rt.color[0]->d.width;
    uint32_t h = g_rt.color[0]->d.height;
    if (w == 1024 && h == 1024) return 0; // Skip 1024x1024 shadow map passes
    return 1;
}

// Per-serial linear RGBA8 shadow buffer for swapchain and render target textures
#define WIN_TEX_SLOTS 64
typedef struct {
    uint64_t serial;
    uint32_t w, h;
    uint32_t *bgra; // w * h pixels in 0xAARRGGBB (little-endian B,G,R,A)
    int dirty;
} win_tex_slot;
static win_tex_slot g_wtex[WIN_TEX_SLOTS];
static int g_wtex_next;
static pthread_mutex_t g_wtex_mu = PTHREAD_MUTEX_INITIALIZER;

static const tex_t *g_tex_by_id[65536];
static const tex_t *g_stage_tex[6][256];

static win_tex_slot *wtex_find_or_make(const tex_t *t, int make) {
    if (!t || !t->serial || t->d.width == 0 || t->d.height == 0 || t->d.width > 4096 || t->d.height > 4096) return NULL;
    for (int i = 0; i < WIN_TEX_SLOTS; i++) {
        if (g_wtex[i].serial == t->serial) {
            if (g_wtex[i].w == t->d.width && g_wtex[i].h == t->d.height && g_wtex[i].bgra)
                return &g_wtex[i];
            free(g_wtex[i].bgra);
            g_wtex[i].w = t->d.width;
            g_wtex[i].h = t->d.height;
            g_wtex[i].bgra = calloc((size_t)t->d.width * t->d.height, 4);
            return g_wtex[i].bgra ? &g_wtex[i] : NULL;
        }
    }
    if (!make) return NULL;
    int idx = g_wtex_next++ % WIN_TEX_SLOTS;
    free(g_wtex[idx].bgra);
    g_wtex[idx].serial = t->serial;
    g_wtex[idx].w = t->d.width;
    g_wtex[idx].h = t->d.height;
    g_wtex[idx].bgra = calloc((size_t)t->d.width * t->d.height, 4);
    g_wtex[idx].dirty = 0;
    return g_wtex[idx].bgra ? &g_wtex[idx] : NULL;
}

static uint32_t rgba_float_to_bgra8(const float rgba[4]) {
    float r = rgba[0] < 0.f ? 0.f : (rgba[0] > 1.f ? 1.f : rgba[0]);
    float g = rgba[1] < 0.f ? 0.f : (rgba[1] > 1.f ? 1.f : rgba[1]);
    float b = rgba[2] < 0.f ? 0.f : (rgba[2] > 1.f ? 1.f : rgba[2]);
    float a = rgba[3] < 0.f ? 0.f : (rgba[3] > 1.f ? 1.f : rgba[3]);
    uint32_t R = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t G = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t B = (uint32_t)(b * 255.0f + 0.5f);
    uint32_t A = (uint32_t)(a * 255.0f + 0.5f);
    return (A << 24) | (R << 16) | (G << 8) | B;
}

static inline uint8_t half_to_u8(uint16_t h) {
    uint32_t sign = (h >> 15) & 1;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    if (exp == 0) return 0;
    if (exp == 31) return sign ? 0 : 255;
    int e = (int)exp - 15;
    if (e < -8) return 0;
    if (e >= 0) return sign ? 0 : 255;
    uint32_t val = ((1u << 10) | mant) >> (-e + 2);
    return val > 255 ? 255 : (uint8_t)val;
}

static inline uint8_t u11_to_u8(uint32_t val) {
    uint32_t exp = (val >> 6) & 0x1f;
    uint32_t mant = val & 0x3f;
    if (exp == 0) return 0;
    if (exp >= 15) return 255;
    int e = (int)exp - 15;
    if (e < -6) return 0;
    return (uint8_t)(((1u << 6) | mant) >> (-e + 2));
}

static inline uint8_t u10_to_u8(uint32_t val) {
    uint32_t exp = (val >> 5) & 0x1f;
    uint32_t mant = val & 0x1f;
    if (exp == 0) return 0;
    if (exp >= 15) return 255;
    int e = (int)exp - 15;
    if (e < -5) return 0;
    return (uint8_t)(((1u << 5) | mant) >> (-e + 2));
}

static int tex_to_bgra(const tex_t *tex, uint32_t *dst, int dw, int dh) {
    if (!tex || !tex->mem || tex->d.width == 0 || tex->d.height == 0 || dw <= 0 || dh <= 0) return 0;
    uint32_t row_bytes = 0, rows = 0, slices = 0, bpb = 0;
    uint64_t base = 0; unsigned th = 0, td = 0;
    if (nvn_deswizzle_geom(&tex->d, 0, 0, &row_bytes, &rows, &slices, &base, &th, &td, &bpb) != 0 || row_bytes == 0 || rows == 0)
        return 0;

    static int logged_call = 0;
    if (logged_call < 5) {
        logged_call++;
        const uint8_t *m = (const uint8_t *)tex->mem;
        LOG("[nvn-win] tex_to_bgra #%d tex=%p (%ux%u fmt=%#x) row_bytes=%u rows=%u mem[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x",
            logged_call, (void*)tex, tex->d.width, tex->d.height, tex->d.format, row_bytes, rows,
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
    }

    uint8_t *lin = malloc((size_t)row_bytes * rows);
    if (!lin) return 0;
    if (nvn_deswizzle(&tex->d, 0, 0, tex->mem, lin, row_bytes) != 0) {
        free(lin);
        return 0;
    }

    uint32_t tw = tex->d.width, th_tex = tex->d.height;
    int fmt = (int)tex->d.format;
    int non_zero = 0;

    for (int dy = 0; dy < dh; dy++) {
        uint32_t sy = (uint32_t)((uint64_t)dy * th_tex / dh);
        if (sy >= th_tex) sy = th_tex - 1;
        for (int dx = 0; dx < dw; dx++) {
            uint32_t sx = (uint32_t)((uint64_t)dx * tw / dw);
            if (sx >= tw) sx = tw - 1;

            uint32_t c = 0;
            if (fmt == 0x25 || fmt == 0x26) {
                const uint8_t *p = lin + (size_t)sy * row_bytes + (size_t)sx * 4;
                c = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            } else if (fmt == 0x37 || fmt == 0x38) {
                const uint8_t *p = lin + (size_t)sy * row_bytes + (size_t)sx * 4;
                c = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
            } else if (fmt == 0x29 || fmt == 0x2A) {
                const uint16_t *p16 = (const uint16_t *)(lin + (size_t)sy * row_bytes + (size_t)sx * 8);
                uint8_t r = half_to_u8(p16[0]), g = half_to_u8(p16[1]), b = half_to_u8(p16[2]), a = half_to_u8(p16[3]);
                c = ((uint32_t)(a ? a : 0xFF) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else if (fmt == 0x3F) {
                uint32_t packed = *(const uint32_t *)(lin + (size_t)sy * row_bytes + (size_t)sx * 4);
                uint8_t r = u11_to_u8(packed & 0x7FF);
                uint8_t g = u11_to_u8((packed >> 11) & 0x7FF);
                uint8_t b = u10_to_u8((packed >> 22) & 0x3FF);
                c = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
            if (c & 0x00FFFFFFu) non_zero = 1;
            if (c != 0) dst[dy * dw + dx] = c;
        }
    }
    free(lin);
    return non_zero;
}

#define SHOTQ_MAX 8
static struct { uint64_t frame; char path[1024]; } g_shotq[SHOTQ_MAX];
static int g_nshotq;
static pthread_mutex_t g_shotq_mu = PTHREAD_MUTEX_INITIALIZER;

int mtl_pso_prebuild_tool(void) { return 0; }
void dm_gfx_shot_at(long frames, const char *path) {
    if (!path) return;
    pthread_mutex_lock(&g_shotq_mu);
    if (g_nshotq < SHOTQ_MAX) {
        g_shotq[g_nshotq].frame = atomic_load(&g_present_n) + (uint64_t)(frames > 0 ? frames : 0);
        snprintf(g_shotq[g_nshotq].path, sizeof g_shotq[0].path, "%s", path);
        g_nshotq++;
    }
    pthread_mutex_unlock(&g_shotq_mu);
}
void dm_gfx_texlist(int target) { (void)target; }
void dm_gfx_texraw(uint64_t serial, const char *path) { (void)serial; (void)path; }
void dm_gfx_texdump_next(const char *path) { (void)path; }
void dm_gfx_passdump_next(long n) { (void)n; }

void mtl_init(void) {}
void mtl_vbuf_stats(uint64_t *resolve, uint64_t *reuse, uint64_t *set, uint64_t *skip, uint64_t *copies, uint64_t *copy_bytes) {
    if (resolve) *resolve = 0;
    if (reuse) *reuse = 0;
    if (set) *set = 0;
    if (skip) *skip = 0;
    if (copies) *copies = 0;
    if (copy_bytes) *copy_bytes = 0;
}
void mtl_set_depth_zero(int on) { (void)on; }
void mtl_set_origin_lower_left(int on) { (void)on; }
void bf_want_note(const char *key) { (void)key; }

void mtl_pool_add(const void *mem, uint64_t size) { (void)mem; (void)size; }
void mtl_pool_del(const void *mem) { (void)mem; }
void mtl_alias_drop_pool(const void *pool) { (void)pool; }
int mtl_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off) {
    (void)len;
    if (buf) *buf = (void *)(uintptr_t)addr;
    if (off) *off = 0;
    return addr != 0;
}

void mtl_res_counts(uint32_t tick, const char *why) { (void)tick; (void)why; }
void mtl_res_snapshot(mtl_res_count *out) { if (out) memset(out, 0, sizeof *out); }

void mtl_sub_begin(void) {
    atomic_fetch_add(&g_stat_submit, 1);
}
void mtl_sub_copy(uint64_t src, uint64_t dst, uint64_t n) {
    if (src && dst && n) memcpy((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, (size_t)n);
}
void mtl_sub_event(volatile uint32_t *val, uint32_t mode, uint32_t v) {
    if (!val) return;
    if (mode == 1) *val += v;
    else *val = v;
}
void mtl_sub_counter(uint64_t addr, uint32_t type) {
    if (!addr) return;
    uint64_t *dst = (uint64_t *)(uintptr_t)addr;
    dst[0] = 0;
    dst[1] = guest_ns();
    (void)type;
}
uint64_t mtl_sub_commit(void) {
    return atomic_fetch_add(&g_fence_cnt, 1) + 1;
}

uint64_t mtl_last_fence(void) { return atomic_load(&g_fence_cnt); }
int mtl_wait(uint64_t fence, uint64_t timeout_ns) { (void)fence; (void)timeout_ns; return 1; }
int mtl_wait_acq(uint64_t fence, uint64_t timeout_ns) { (void)fence; (void)timeout_ns; return 1; }
void mtl_finish(void) {}
void mtl_drain(void) {}
void mtl_xv_window_tex(uint64_t serial) { (void)serial; }
void *mtl_device(void) { return NULL; }
void *mtl_device_classic(void) { return NULL; }
uint64_t mtl_ext_tex(void *tex) { (void)tex; return 0; }
uint64_t mtl_present_ext(uint64_t serial, double period, void *ev, uint64_t ev_val, const int crop[4]) {
    (void)serial; (void)period; (void)ev; (void)ev_val; (void)crop; return 0;
}
void mtl_present_wait(uint64_t ticket) { (void)ticket; }

void mtl_sub_fill(uint64_t dst, uint64_t n, uint32_t value) {
    if (!dst || !n) return;
    uint8_t b0 = value & 0xff;
    if (value == ((uint32_t)b0 * 0x01010101u)) {
        memset((void *)(uintptr_t)dst, b0, (size_t)n);
        return;
    }
    uint32_t *p = (uint32_t *)(uintptr_t)dst;
    uint64_t words = n / 4;
    for (uint64_t i = 0; i < words; i++) p[i] = value;
}

void mtl_exec(const uint8_t *p) {
    if (!p) return;
    for (;;) {
        op_hdr h;
        memcpy(&h, p, sizeof h);
        if (h.size < 8) return;
        const void *a = p + 8;
        switch (h.op) {
            case OP_END:
                return;
            case OP_BEGIN:
                break;
            case OP_JUMP: {
                const uint8_t *t;
                memcpy(&t, a, 8);
                if (!t) return;
                p = t;
                continue;
            }
            case OP_CALL: {
                uint64_t t;
                memcpy(&t, a, 8);
                if (t) mtl_exec((const uint8_t *)(uintptr_t)t);
                break;
            }
            case OP_SIGNAL_EVENT: {
                volatile uint32_t *val;
                uint32_t m, v;
                memcpy(&val, a, 8);
                memcpy(&m, (const uint8_t *)a + 8, 4);
                memcpy(&v, (const uint8_t *)a + 12, 4);
                mtl_sub_event(val, m, v);
                break;
            }
            case OP_REPORT_COUNTER: {
                uint64_t t, ad;
                memcpy(&t, a, 8);
                memcpy(&ad, (const uint8_t *)a + 8, 8);
                mtl_sub_counter(ad, (uint32_t)t);
                break;
            }
            case OP_FENCE_SYNC: {
                sync_t *s;
                memcpy(&s, a, 8);
                if (s) {
                    s->fence = atomic_load(&g_fence_cnt) + 1;
                    s->fenced = 1;
                }
                break;
            }
            case OP_RESET_COUNTER:
                break;
            case OP_CLEAR_BUF: {
                const p_clear_buf *cb = a;
                mtl_sub_fill(cb->addr, cb->size, cb->value);
                break;
            }
            case OP_COPY_B2B: {
                uint64_t s_, d, n;
                memcpy(&s_, a, 8);
                memcpy(&d, (const uint8_t *)a + 8, 8);
                memcpy(&n, (const uint8_t *)a + 16, 8);
                mtl_sub_copy(s_, d, n);
                break;
            }
            case OP_UPDATE_UB: {
                uint64_t d, n;
                memcpy(&d, a, 8);
                memcpy(&n, (const uint8_t *)a + 8, 8);
                if (d && n) memcpy((void *)(uintptr_t)d, (const uint8_t *)a + 16, (size_t)n);
                break;
            }
            case OP_SET_RT: {
                const p_set_rt *r = a;
                g_rt = *r;
                g_has_rt = 1;
                atomic_fetch_add(&g_stat_pass, 1);
                if (r->n > 0 && r->color[0]) {
                    g_last_rendered_rt = r->color[0];
                    if (is_main_rt()) {
                        g_main_scene_rt = r->color[0];
                        vk_win_set_viewport(0, 0, (float)r->color[0]->d.width, (float)r->color[0]->d.height);
                        vk_win_set_scissor(0, 0, r->color[0]->d.width, r->color[0]->d.height);
                    }
                    uint64_t cur_pn = atomic_load(&g_present_n);
                    static int logged_rt = 0;
                    if (logged_rt < 60 || cur_pn == 60) {
                        logged_rt++;
                        LOG("[nvn-win] SET_RT #%d [f=%llu] color0=%p (w=%u h=%u fmt=%#x mem=%p)",
                            logged_rt, (unsigned long long)cur_pn, (void*)r->color[0], r->color[0]->d.width, r->color[0]->d.height, r->color[0]->d.format, r->color[0]->mem);
                    }
                }
                break;
            }
            case OP_VIEWPORTS: {
                const p_viewports *vp = a;
                if (vp && vp->n > 0 && is_main_rt()) {
                    vk_win_set_viewport(vp->v[0][0], vp->v[0][1], vp->v[0][2], vp->v[0][3]);
                }
                break;
            }
            case OP_SCISSOR: {
                const p_scissor *sc = a;
                if (sc && is_main_rt()) {
                    vk_win_set_scissor(sc->x, sc->y, (uint32_t)sc->w, (uint32_t)sc->h);
                }
                break;
            }
            case OP_CLEAR_COLOR: {
                const p_clear_color *c = a;
                atomic_fetch_add(&g_stat_clear, 1);
                uint32_t bgra = rgba_float_to_bgra8(c->rgba);
                if (c->index == 0) atomic_store(&g_last_clear_bgra, bgra | 0xFF000000u);
                if (g_has_rt && c->index >= 0 && c->index < g_rt.n && g_rt.color[c->index]) {
                    const tex_t *rt = g_rt.color[c->index];
                    pthread_mutex_lock(&g_wtex_mu);
                    win_tex_slot *s = wtex_find_or_make(rt, 1);
                    if (s && s->bgra) {
                        size_t npix = (size_t)s->w * s->h;
                        for (size_t i = 0; i < npix; i++) s->bgra[i] = bgra;
                        s->dirty = 1;
                    }
                    pthread_mutex_unlock(&g_wtex_mu);
                }
                break;
            }
            case OP_CLEAR_TEX: {
                const p_clear_tex *c = a;
                atomic_fetch_add(&g_stat_clear, 1);
                if (c->tex && !c->is_uint) {
                    uint32_t bgra = rgba_float_to_bgra8(c->c.f);
                    pthread_mutex_lock(&g_wtex_mu);
                    win_tex_slot *s = wtex_find_or_make(c->tex, 1);
                    if (s && s->bgra) {
                        size_t npix = (size_t)s->w * s->h;
                        for (size_t i = 0; i < npix; i++) s->bgra[i] = bgra;
                        s->dirty = 1;
                    }
                    pthread_mutex_unlock(&g_wtex_mu);
                }
                break;
            }
            case OP_COPY_B2T: {
                const p_copy_b2t *c = a;
                if (c->tex && c->src) {
                    unsigned lv = c->has_view && c->view.set_levels ? c->view.base_level : 0;
                    unsigned l0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
                    nvn_tex_mirror_b2t(c->tex, lv, l0, &c->r, (const uint8_t *)(uintptr_t)c->src, 0, 0);
                }
                break;
            }
            case OP_COPY_T2T: {
                const p_copy_t2t *c = a;
                if (c->src && c->dst) {
                    unsigned sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0;
                    unsigned sz0 = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0;
                    unsigned dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
                    unsigned dz0 = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
                    nvn_tex_mirror_t2t(c->src, sl, sz0, &c->sr, c->dst, dl, dz0, &c->dr);

                    static int logged_t2t = 0;
                    if (logged_t2t < 5) {
                        logged_t2t++;
                        LOG("[nvn-win] COPY_T2T src=%p (%ux%u) -> dst=%p (%ux%u)",
                            (void*)c->src, c->src->d.width, c->src->d.height,
                            (void*)c->dst, c->dst->d.width, c->dst->d.height);
                    }

                    pthread_mutex_lock(&g_wtex_mu);
                    win_tex_slot *ss = wtex_find_or_make(c->src, 0);
                    if (ss && ss->dirty && ss->bgra) {
                        win_tex_slot *ds = wtex_find_or_make(c->dst, 1);
                        if (ds && ds->bgra && ds->w > 0 && ds->h > 0) {
                            if (ds->w == ss->w && ds->h == ss->h) {
                                memcpy(ds->bgra, ss->bgra, (size_t)ds->w * ds->h * 4);
                            } else {
                                for (int dy = 0; dy < ds->h; dy++) {
                                    int sy = dy * ss->h / ds->h;
                                    for (int dx = 0; dx < ds->w; dx++) {
                                        int sx = dx * ss->w / ds->w;
                                        ds->bgra[dy * ds->w + dx] = ss->bgra[sy * ss->w + sx];
                                    }
                                }
                            }
                            ds->dirty = 1;
                        }
                    }
                    pthread_mutex_unlock(&g_wtex_mu);
                }
                break;
            }
            case OP_BIND_PROGRAM: {
                const p_bind_program *b = a;
                if (b->prog) {
                    uint8_t k0[8] = {0}, k1[8] = {0};
                    char s0[17] = "none", s1[17] = "none";
                    if (prog_key(b->prog, 0, k0)) {
                        for (int j = 0; j < 8; j++) snprintf(s0 + j * 2, 3, "%02x", k0[j]);
                    }
                    if (prog_key(b->prog, 1, k1)) {
                        for (int j = 0; j < 8; j++) snprintf(s1 + j * 2, 3, "%02x", k1[j]);
                    }
                    char spv0[512], spv1[512];
                    const char *pdir = getenv("DM_GAMES_DIR");
                    const char *gid = getenv("DM_GAME");
                    if (pdir && gid) {
                        snprintf(spv0, sizeof spv0, "%s\\%s\\spirv\\%s.spv", pdir, gid, s0);
                        snprintf(spv1, sizeof spv1, "%s\\%s\\spirv\\%s.spv", pdir, gid, s1);
                        FILE *f0 = fopen(spv0, "rb");
                        FILE *f1 = fopen(spv1, "rb");
                        if (f0) fclose(f0);
                        if (f1) fclose(f1);
                        static int logged_spv = 0;
                        uint64_t cur_pn = atomic_load(&g_present_n);
                        if (logged_spv < 100 || cur_pn == 60) {
                            logged_spv++;
                            LOG("[nvn-win] BIND_PROGRAM #%d [f=%llu] vs=%s (spv=%s) ps=%s (spv=%s)",
                                logged_spv, (unsigned long long)cur_pn,
                                s0, f0 ? "READY" : "MISSING",
                                s1, f1 ? "READY" : "MISSING");
                        }
                        vk_win_bind_program(s0, s1);
                    }
                }
                break;
            }
            case OP_BIND_VATTR: {
                const p_bind_vattr *b = a;
                static int logged_vattr = 0;
                if (logged_vattr < 15) {
                    logged_vattr++;
                    for (int i = 0; i < b->n; i++) {
                        uint32_t val = b->st[i];
                        uint32_t fmt = val & 0xFF;
                        uint32_t off = (val >> 8) & 0x7FFFF;
                        uint32_t str = val >> 27;
                        LOG("[nvn-win] BIND_VATTR #%d [%d]: fmt=%#x off=%u stream=%u (raw=%#x)",
                            logged_vattr, b->first + i, fmt, off, str, val);
                    }
                }
                vk_win_set_vattr(b->first, b->n, b->st);
                break;
            }
            case OP_BIND_VSTREAM: {
                const p_bind_vstream *b = a;
                static int logged_vstream = 0;
                if (logged_vstream < 15) {
                    logged_vstream++;
                    for (int i = 0; i < b->n; i++) {
                        LOG("[nvn-win] BIND_VSTREAM #%d [%d]: stride=%u divisor=%u",
                            logged_vstream, b->first + i, b->st[i].stride, b->st[i].divisor);
                    }
                }
                vk_win_set_vstream(b->first, b->n, b->st);
                break;
            }
            case OP_BIND_VBUF:
            case OP_BIND_VBUF1: {
                p_bind_vbuf tmp;
                const p_bind_vbuf *b = vbuf_rec(h.op, a, &tmp);
                static int logged_vbuf = 0;
                if (logged_vbuf < 15) {
                    logged_vbuf++;
                    for (int i = 0; i < b->n; i++) {
                        LOG("[nvn-win] BIND_VBUF #%d [%d]: addr=%#llx size=%llu",
                            logged_vbuf, b->first + i,
                            (unsigned long long)b->r[i].address,
                            (unsigned long long)b->r[i].size);
                    }
                }
                for (int i = 0; i < b->n; i++) {
                    vk_win_set_vbuf(b->first + i, (const void *)(uintptr_t)b->r[i].address, (size_t)b->r[i].size);
                }
                break;
            }
            case OP_BIND_UBO:
            case OP_BIND_UBO_SMALL: {
                p_bind_buf tmp;
                const p_bind_buf *b = bind_buf_rec(h.op, a, &tmp);
                if (b && b->stage >= 0 && b->stage < 6 && b->index >= 0 && b->index < 16) {
                    static int logged_ubo = 0;
                    uint64_t cur_pn = atomic_load(&g_present_n);
                    if (logged_ubo < 10 || cur_pn == 60) {
                        logged_ubo++;
                        LOG("[nvn-win] BIND_UBO [f=%llu] stage=%d index=%d addr=%#llx size=%llu",
                            (unsigned long long)cur_pn, b->stage, b->index, (unsigned long long)b->addr, (unsigned long long)b->size);
                    }
                    vk_win_set_ubo(b->stage, b->index, (const void *)(uintptr_t)b->addr, (size_t)b->size);
                }
                break;
            }
            case OP_BIND_TEX:
            case OP_BIND_TEX1: {
                p_bind_handles tmp;
                const p_bind_handles *b = bind_rec(h.op, a, &tmp);
                if (b && b->stage >= 0 && b->stage < 6) {
                    if (b->defer && b->src && b->n > 0 && b->n <= 32) {
                        memcpy((void *)b->h, (const void *)(uintptr_t)b->src, 8 * (size_t)b->n);
                    }
                    for (int i = 0; i < b->n; i++) {
                        uint32_t tid = (uint32_t)(b->h[i] & 0xfffff);
                        if (tid < 65536 && b->first + i < 256) {
                            g_stage_tex[b->stage][b->first + i] = g_tex_by_id[tid];
                            vk_win_set_tex(b->stage, b->first + i, g_tex_by_id[tid]);
                            const tex_t *tex = g_tex_by_id[tid];
                            static int logged_bind_tex = 0;
                            if (logged_bind_tex < 60) {
                                logged_bind_tex++;
                                LOG("[nvn-win] BIND_TEX #%d stage=%d slot=%d tid=%u tex=%p (w=%u h=%u fmt=%#x mem=%p)",
                                    logged_bind_tex, b->stage, b->first + i, tid, (void*)tex,
                                    tex ? tex->d.width : 0, tex ? tex->d.height : 0, tex ? tex->d.format : 0, tex ? tex->mem : NULL);
                            }
                        }
                    }
                }
                break;
            }
            case OP_DRAW: {
                atomic_fetch_add(&g_stat_draw, 1);
                p_draw t;
                const p_draw *d = draw_rec(a, h.size, &t);
                if (is_main_rt()) {
                    vk_win_draw((uint32_t)d->first, (uint32_t)d->count);
                }
                if (g_has_rt && g_rt.n > 0 && g_rt.color[0]) {
                    const tex_t *rt = g_rt.color[0];
                    pthread_mutex_lock(&g_wtex_mu);
                    win_tex_slot *s = wtex_find_or_make(rt, 1);
                    if (s && s->bgra) {
                        const tex_t *tex = g_stage_tex[1][8] ? g_stage_tex[1][8] : (g_stage_tex[1][0] ? g_stage_tex[1][0] : g_stage_tex[0][0]);
                        static int logged_draw = 0;
                        uint64_t cur_pn = atomic_load(&g_present_n);
                        if (logged_draw < 100 || cur_pn == 60) {
                            logged_draw++;
                            LOG("[nvn-win] DRAW #%d [f=%llu] prim=%d count=%d tex0=%p tex8=%p (w=%u h=%u fmt=%#x mem=%p)",
                                logged_draw, (unsigned long long)cur_pn, d->prim, d->count,
                                (void*)g_stage_tex[1][0], (void*)g_stage_tex[1][8],
                                tex ? tex->d.width : 0,
                                tex ? tex->d.height : 0,
                                tex ? tex->d.format : 0,
                                tex ? tex->mem : NULL);
                            if (tex && tex->mem) {
                                const uint8_t *m = (const uint8_t *)tex->mem;
                                LOG("[nvn-win] DRAW #%d tex mem[0..15]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                                    logged_draw, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
                            }
                        }
                        if (tex) {
                            if (tex_to_bgra(tex, s->bgra, s->w, s->h)) {
                                s->dirty = 1;
                            }
                        } else {
                            s->dirty = 1;
                        }
                    }
                    pthread_mutex_unlock(&g_wtex_mu);
                }
                break;
            }
            case OP_DRAW_IDX: {
                atomic_fetch_add(&g_stat_draw, 1);
                p_draw_idx t;
                const p_draw_idx *d = draw_idx_rec(a, h.size, &t);

                if (is_main_rt()) {
                    vk_win_draw_indexed(d->prim, d->itype, (uint32_t)d->count, (const void *)(uintptr_t)d->ibuf, d->base_vertex);
                }
                if (g_has_rt && g_rt.n > 0 && g_rt.color[0]) {
                    const tex_t *rt = g_rt.color[0];
                    pthread_mutex_lock(&g_wtex_mu);
                    win_tex_slot *s = wtex_find_or_make(rt, 1);
                    if (s && s->bgra) {
                        const tex_t *tex = g_stage_tex[1][0] ? g_stage_tex[1][0] : g_stage_tex[0][0];
                        static int logged_draw_idx = 0;
                        uint64_t cur_pn = atomic_load(&g_present_n);
                        if (logged_draw_idx < 20 || cur_pn == 60) {
                            logged_draw_idx++;
                            const uint16_t *idx16 = (const uint16_t *)(uintptr_t)d->ibuf;
                            LOG("[nvn-win] DRAW_IDX #%d [f=%llu] prim=%d count=%d itype=%d ibuf=%#llx bv=%d idx[0..5]=%u,%u,%u,%u,%u,%u tex=%p (w=%u h=%u fmt=%#x)",
                                logged_draw_idx, (unsigned long long)cur_pn, d->prim, d->count, d->itype, (unsigned long long)d->ibuf, d->base_vertex,
                                (d->ibuf && d->count >= 6) ? idx16[0] : 0,
                                (d->ibuf && d->count >= 6) ? idx16[1] : 0,
                                (d->ibuf && d->count >= 6) ? idx16[2] : 0,
                                (d->ibuf && d->count >= 6) ? idx16[3] : 0,
                                (d->ibuf && d->count >= 6) ? idx16[4] : 0,
                                (d->ibuf && d->count >= 6) ? idx16[5] : 0,
                                (void*)tex,
                                tex ? tex->d.width : 0,
                                tex ? tex->d.height : 0,
                                tex ? tex->d.format : 0);
                        }
                        if (tex) {
                            if (tex_to_bgra(tex, s->bgra, s->w, s->h)) {
                                s->dirty = 1;
                            }
                        } else {
                            s->dirty = 1;
                        }
                    }
                    pthread_mutex_unlock(&g_wtex_mu);
                }
                break;
            }
            default:
                break;
        }
        p += h.size;
    }
}

void mtl_exec_rest(const uint64_t *handles, int n) { (void)handles; (void)n; }

uint64_t mtl_present(const tex_t *t, double period_s, const int crop[4]) {
    (void)period_s; (void)crop;
    uint64_t pn = atomic_fetch_add(&g_present_n, 1) + 1;
    if (pn <= 10 || (pn % 60) == 0) {
        LOG("[nvn-win] PRESENT frame %llu (guest_frame=%llu, stat_draw=%llu, stat_pass=%llu)",
            (unsigned long long)pn, (unsigned long long)dm_guest_frame(),
            (unsigned long long)atomic_load(&g_stat_draw), (unsigned long long)atomic_load(&g_stat_pass));
    }
    uint64_t f = atomic_load(&g_cur_frame);
    uint64_t subs = atomic_load(&g_stat_submit);
    uint64_t drs = atomic_load(&g_stat_draw);
    uint64_t clrs = atomic_load(&g_stat_clear);
    uint32_t cbgra = atomic_load(&g_last_clear_bgra);

    uint32_t *bgra = NULL;
    int w = 0, h = 0;

    if (t && t->d.width > 0 && t->d.height > 0 && t->d.width <= 4096 && t->d.height <= 4096) {
        static int logged_fmt = 0;
        if (!logged_fmt) {
            logged_fmt = 1;
            LOG("[nvn-win] mtl_present first frame tex=%p w=%u h=%u fmt=%#x mem=%p", (void *)t, t->d.width, t->d.height, t->d.format, t->mem);
        }
        if (t->mem) {
            const uint8_t *tm = (const uint8_t *)t->mem;
            static int logged_pmem = 0;
            if (logged_pmem < 5) {
                logged_pmem++;
                LOG("[nvn-win] mtl_present #%d tex mem[0..15]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    logged_pmem, tm[0], tm[1], tm[2], tm[3], tm[4], tm[5], tm[6], tm[7], tm[8], tm[9], tm[10], tm[11], tm[12], tm[13], tm[14], tm[15]);
            }
        }
        w = (int)t->d.width;
        h = (int)t->d.height;
        if (!vk_win_is_active()) {
            pthread_mutex_lock(&g_wtex_mu);
            win_tex_slot *s = wtex_find_or_make(t, 1);
            if (s && s->bgra) {
                if (t->mem) {
                    if (tex_to_bgra(t, s->bgra, w, h)) {
                        s->dirty = 1;
                    }
                }
                if (!s->dirty) {
                    const tex_t *source_rt = g_main_scene_rt ? g_main_scene_rt : g_last_rendered_rt;
                    if (source_rt) {
                        win_tex_slot *rts = wtex_find_or_make(source_rt, 0);
                        if (rts && rts->dirty && rts->bgra && rts->w > 0 && rts->h > 0) {
                            int sw = rts->w, sh = rts->h;
                            for (int dy = 0; dy < h; dy++) {
                                int sy = dy * sh / h;
                                for (int dx = 0; dx < w; dx++) {
                                    int sx = dx * sw / w;
                                    s->bgra[dy * w + dx] = rts->bgra[sy * sw + sx];
                                }
                            }
                            s->dirty = 1;
                        } else if (source_rt->mem) {
                            if (tex_to_bgra(source_rt, s->bgra, w, h)) {
                                s->dirty = 1;
                            }
                        }
                    }
                }
                if (s->dirty) {
                    bgra = malloc((size_t)w * h * 4);
                    if (bgra) memcpy(bgra, s->bgra, (size_t)w * h * 4);
                }
            }
            pthread_mutex_unlock(&g_wtex_mu);
        }
    }

    // Check DM_NVN_SHOT and queued dm_gfx_shot_at requests
    static long shots[64];
    static int nshot = -1;
    static const char *shot_path;
    if (nshot < 0) {
        nshot = 0;
        const char *e = getenv("DM_NVN_SHOT"), *c = e ? strchr(e, ':') : NULL;
        if (c) {
            shot_path = c + 1;
            for (const char *q = e; q < c && nshot < 64; q = strchr(q, ',') ? strchr(q, ',') + 1 : c)
                shots[nshot++] = atol(q);
            LOG("[nvn-win] parsed DM_NVN_SHOT: nshot=%d shot_path=%s shots[0]=%ld", nshot, shot_path, nshot > 0 ? shots[0] : -1);
        }
    }
    int want_shot = 0;
    for (int i = 0; i < nshot; i++) {
        if ((long)pn == shots[i]) { want_shot = 1; break; }
    }
    if (want_shot) vk_win_request_capture();

    front_win_present_bgra(bgra, w, h, f, subs, drs, clrs, cbgra);
    free(bgra);
    for (int i = 0; i < nshot; i++) {
        if ((long)pn == shots[i]) {
            char path[1024];
            snprintf(path, sizeof path, shot_path, (int)pn);
            int ok = front_win_capture_bmp(path);
            LOG("[nvn-win] screenshot frame %llu -> %s (%s)", (unsigned long long)pn, path, ok ? "ok" : "failed");
        }
    }
    pthread_mutex_lock(&g_shotq_mu);
    for (int i = 0; i < g_nshotq;) {
        if (pn >= g_shotq[i].frame) {
            int ok = front_win_capture_bmp(g_shotq[i].path);
            LOG("[nvn-win] screenshot frame %llu -> %s (%s)", (unsigned long long)pn, g_shotq[i].path, ok ? "ok" : "failed");
            g_shotq[i] = g_shotq[--g_nshotq];
        } else i++;
    }
    pthread_mutex_unlock(&g_shotq_mu);

    static long g_max_frames = -1;
    if (g_max_frames < 0) {
        const char *df = getenv("DM_FRAMES");
        g_max_frames = df ? atol(df) : 0;
    }
    if (g_max_frames > 0 && (long)pn >= g_max_frames) {
        LOG("[nvn-win] DM_FRAMES limit %ld reached at frame %llu, exiting cleanly", g_max_frames, (unsigned long long)pn);
        exit(0);
    }

    return 0;
}

void mtl_present_flush(void) {}
void mtl_present_test_stall(uint64_t ns) { (void)ns; }
int mtl_display_hz(void) { return front_metal_layer() ? 60 : 0; }
void mtl_set_tex_pool_n(int n) { (void)n; }
void mtl_set_frame(uint64_t f) { atomic_store(&g_cur_frame, f); }
void mtl_ub_present(double iv_ms) { (void)iv_ms; }
double mtl_gpu_ms(uint64_t f) { (void)f; return -1.0; }
void mtl_ab_gpu(uint64_t f, double gpu_ms, double frame_ms) { (void)f; (void)gpu_ms; (void)frame_ms; }
void mtl_sub_index(const uint64_t *h, int n) { (void)h; (void)n; }
void xv_test_counts(int *dc, int *viol) { if (dc) *dc = 0; if (viol) *viol = 0; }
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1) { (void)f; (void)t0; (void)t1; return 0; }
void mtl_stats(uint64_t *passes, uint64_t *draws) {
    if (passes) *passes = atomic_load(&g_stat_pass);
    if (draws) *draws = atomic_load(&g_stat_draw);
}
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { if (enc) *enc = 0; if (reuse) *reuse = 0; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { if (call) *call = 0; if (skip) *skip = 0; }
uint64_t mtl_ds_omit(void) { return 0; }
uint64_t mtl_drawable_ns_take(void) { return 0; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full_rw_passes) {
    if (load) *load = 0;
    if (store) *store = 0;
    if (full_rw_passes) *full_rw_passes = 0;
}
uint64_t mtl_udc_saved_bytes(void) { return 0; }
int mtl_has_window(void) { return front_metal_layer() != NULL; }
int mtl_fps_target(void) { return 60; }
int mtl_warm_wait(int timeout_ms) { (void)timeout_ms; return 1; }
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(int vsyncs, uint64_t timeout_ns) { (void)vsyncs; (void)timeout_ns; return 0; }
int mtl_link_wait_ticks(int ticks, uint64_t timeout_ns) { (void)ticks; (void)timeout_ns; return 0; }
int mtl_link_rate(void) { return 60; }
double mtl_presented_time(uint64_t n) { (void)n; return 0.0; }
int mtl_link_tick(uint64_t n, double *target, double *lead_ms, int *ticks, double *gap_ms) {
    (void)n; (void)target; (void)lead_ms; (void)ticks; (void)gap_ms; return 0;
}
int mtl_link_stalled(void) { return 0; }
int mtl_snap_on(void) { return 0; }
uint64_t mtl_snap_acquire_wait(void) { return 0; }

void mtl_tex_init(const tex_t *t) { (void)t; }
void mtl_tex_release(tex_t *t) {
    if (!t || !t->serial) return;
    pthread_mutex_lock(&g_wtex_mu);
    for (int i = 0; i < WIN_TEX_SLOTS; i++) {
        if (g_wtex[i].serial == t->serial) {
            free(g_wtex[i].bgra);
            memset(&g_wtex[i], 0, sizeof g_wtex[i]);
        }
    }
    pthread_mutex_unlock(&g_wtex_mu);
}
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads) { if (uploads) *uploads = 0; if (reuploads) *reuploads = 0; }
unsigned mtl_cpu_tex_count(void) { return 0; }
uint64_t mtl_pkg_edge_skips(void) { return 0; }
uint64_t mtl_pkg_miss(void) { return 0; }
uint64_t mtl_upload_ns_take(void) { return 0; }
uint64_t mtl_upload_gpu_ns_take(void) { return 0; }
void mtl_create_stats(uint64_t *lib, uint64_t *pso) { if (lib) *lib = 0; if (pso) *pso = 0; }
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) {
    (void)t; (void)level; (void)layer0; (void)r; (void)dst; (void)row; (void)img;
}

void mtl_reg_tex(int id, const tex_t *t, const view_t *v) {
    (void)v;
    if (id >= 0 && id < 65536) {
        g_tex_by_id[id] = t;
        static int logged_reg = 0;
        if (logged_reg < 20) {
            logged_reg++;
            int nz = 0;
            if (t && t->mem) {
                const uint8_t *m = (const uint8_t *)t->mem;
                for (size_t k = 0; k < 64 && k < t->L.size; k++) if (m[k]) nz = 1;
            }
            LOG("[nvn-win] REG_TEX #%d id=%d tex=%p (w=%u h=%u fmt=%#x mem=%p nz=%d)",
                logged_reg, id, (void*)t, t ? t->d.width : 0, t ? t->d.height : 0, t ? t->d.format : 0, t ? t->mem : NULL, nz);
        }
    }
}
void mtl_reg_image(int id, const tex_t *t, const view_t *v) { (void)id; (void)t; (void)v; }
void mtl_tex_cpu_written(const tex_t *t) { (void)t; }
int mtl_tex_gpu_owned(uint64_t serial) { (void)serial; return 0; }
void mtl_reg_samp(int id, const samp_par *p) { (void)id; (void)p; }
void mtl_prog_release(prog_t *p) { (void)p; }

// Extra statistics functions referenced in hle_nvn.c
void mtl_mem_stat(unsigned long long frame, unsigned long long mb) { (void)frame; (void)mb; }
void mtl_create_ns(uint64_t *lib_ns, uint64_t *pso_ns) { if (lib_ns) *lib_ns = 0; if (pso_ns) *pso_ns = 0; }
void mtl_hybrid_stats(uint64_t *draws, uint64_t *made, uint64_t *none) {
    if (draws) *draws = 0;
    if (made) *made = 0;
    if (none) *none = 0;
}
uint64_t mtl_upload_bytes(void) { return 0; }
void mtl_res_stats(char *out, size_t n) { if (out && n) snprintf(out, n, "win-stub"); }
void mtl_barrier_stats(uint64_t *skip, uint64_t *full) { if (skip) *skip = 0; if (full) *full = 0; }
uint64_t mtl_cut_count(void) { return 0; }
uint64_t mtl_copy_cut_count(void) { return 0; }
uint64_t mtl_blit_splits(void) { return 0; }
void mtl_ubd_stats(uint64_t *a, uint64_t *b) { if (a) *a = 0; if (b) *b = 0; }
void mtl_ubp_stats(uint64_t *a, uint64_t *b, uint64_t *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void mtl_lc_stats(uint64_t *a, uint64_t *b) { if (a) *a = 0; if (b) *b = 0; }
void mtl_ubp_why(uint64_t w[5]) { if (w) memset(w, 0, 5 * sizeof(uint64_t)); }
void mtl_sm_stats(uint64_t *a, uint64_t *b, uint64_t *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void mtl_cshoist_stats(uint64_t *a, uint64_t *b, uint64_t *c) { if (a) *a = 0; if (b) *b = 0; if (c) memset(c, 0, 8 * sizeof(uint64_t)); }
void mtl_pso_line(char *out, size_t n) { if (out && n) snprintf(out, n, "win-stub"); }
void mtl_alloc_stats(uint64_t *aw, uint64_t *ag, int *an) { if (aw) *aw = 0; if (ag) *ag = 0; if (an) *an = 0; }
uint32_t mtl_present_qos(void) { return 0; }

int thread_selfcounts(int type, void *buf, size_t nbytes) {
    (void)type;
    if (buf && nbytes) memset(buf, 0, nbytes);
    return 0;
}

// H.264 VideoToolbox stubs (hle_movie_avc.h)
struct mavc { int dummy; };
mavc *mavc_new(void) { return calloc(1, sizeof(mavc)); }
void mavc_free(mavc *a) { free(a); }
void mavc_param_sets(mavc *a, const uint8_t *p, size_t n) { (void)a; (void)p; (void)n; }
void mavc_avcc(mavc *a, const uint8_t *p, size_t n) { (void)a; (void)p; (void)n; }
int mavc_decode(mavc *a, const uint8_t *p, int n, int64_t pts_us) { (void)a; (void)p; (void)n; (void)pts_us; return 0; }
int mavc_take(mavc *a, int flush, uint8_t *nv12, int w, int h, int64_t *pts_us) {
    (void)a; (void)flush; (void)nv12; (void)w; (void)h; (void)pts_us; return 0;
}
int mavc_queued(const mavc *a) { (void)a; return 0; }
void mavc_set_rgba(mavc *a) { (void)a; }
void mavc_drop(mavc *a) { (void)a; }
