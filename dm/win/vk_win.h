#pragma once
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vk_win_init(HWND hwnd, int width, int height);
void vk_win_shutdown(void);
int vk_win_is_active(void);

// Frame rendering lifecycle
void vk_win_begin_frame(void);
void vk_win_set_ubo(int stage, int index, const void *addr, size_t size);
void vk_win_set_tex(int stage, int slot, const void *tex_ptr);
void vk_win_set_vattr(int first, int n, const uint32_t *st);
void vk_win_set_vstream(int first, int n, const void *st);
void vk_win_set_vbuf(int stream, const void *addr, size_t size);
int vk_win_bind_program(const char *vs_key, const char *ps_key);
void vk_win_set_viewport(float x, float y, float w, float h);
void vk_win_set_scissor(int32_t x, int32_t y, uint32_t w, uint32_t h);
void vk_win_draw(uint32_t first, uint32_t count);
void vk_win_draw_indexed(int prim, int itype, uint32_t count, const void *ibuf, int32_t base_vertex);
void vk_win_end_frame(void);

// Presentation: called on mtl_present
void vk_win_request_capture(void);
void vk_win_present(const uint32_t *bgra, int width, int height);
int vk_win_get_pixels(void **out_pixels, int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif
