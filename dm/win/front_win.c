// Windows front end: opens a 1280x720 Win32 window by default (or when DM_WINDOW=1; DM_HEADLESS=1 disables it),
// maps keyboard and mouse touch to dm_input / dm_touch_set, displays swapchain frames presented by the NVN layer,
// and runs the guest on a 64 MiB stack thread while the main thread runs the Win32 message loop.
#include "../host/front.h"
#include "../host/dm.h"
#include "../host/dm_input.h"
#include "../host/hle.h"
#include "vk_win.h"
#include <windows.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static HWND g_hwnd;
static uint64_t g_btn;
static int g_w, g_a, g_s, g_d;
static int g_input_on = 1;
static int g_mouse_down;

static pthread_mutex_t g_fb_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t *g_fb_pixels;
static int g_fb_w, g_fb_h;
static uint64_t g_fb_frame, g_fb_submits, g_fb_draws, g_fb_clears;
static uint32_t g_fb_clear_bgra = 0xFF1A1E24u; // default dark slate background before first clear

// Dynamic XInput binding for Windows Xbox/PC controllers
typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} WIN_XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    WIN_XINPUT_GAMEPAD Gamepad;
} WIN_XINPUT_STATE;

typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, WIN_XINPUT_STATE *pState);
static PFN_XInputGetState g_pfnXInputGetState;
static int g_xinput_init_done;

static void poll_xinput(uint64_t *out_btn, int32_t *out_lx, int32_t *out_ly, int32_t *out_rx, int32_t *out_ry) {
    if (!g_xinput_init_done) {
        g_xinput_init_done = 1;
        HMODULE hMod = LoadLibraryA("xinput1_4.dll");
        if (!hMod) hMod = LoadLibraryA("xinput1_3.dll");
        if (!hMod) hMod = LoadLibraryA("xinput9_1_0.dll");
        if (hMod) g_pfnXInputGetState = (PFN_XInputGetState)GetProcAddress(hMod, "XInputGetState");
    }
    if (!g_pfnXInputGetState) return;

    WIN_XINPUT_STATE xs;
    memset(&xs, 0, sizeof xs);
    if (g_pfnXInputGetState(0, &xs) != 0) return; // controller 0 not connected or query error

    uint64_t b = 0;
    WORD wb = xs.Gamepad.wButtons;
    // Map to Nintendo Switch layout:
    if (wb & 0x1000) b |= DM_BTN_B;       // Xbox A -> Switch B (bottom)
    if (wb & 0x2000) b |= DM_BTN_A;       // Xbox B -> Switch A (right)
    if (wb & 0x4000) b |= DM_BTN_Y;       // Xbox X -> Switch Y (left)
    if (wb & 0x8000) b |= DM_BTN_X;       // Xbox Y -> Switch X (top)
    if (wb & 0x0100) b |= DM_BTN_L;       // LB -> Switch L
    if (wb & 0x0200) b |= DM_BTN_R;       // RB -> Switch R
    if (wb & 0x0010) b |= DM_BTN_PLUS;    // Start -> Switch Plus
    if (wb & 0x0020) b |= DM_BTN_MINUS;   // Back -> Switch Minus
    if (wb & 0x0040) b |= DM_BTN_LS;      // Left thumb click
    if (wb & 0x0080) b |= DM_BTN_RS;      // Right thumb click
    if (wb & 0x0001) b |= DM_BTN_UP;      // DPad Up
    if (wb & 0x0002) b |= DM_BTN_DOWN;    // DPad Down
    if (wb & 0x0004) b |= DM_BTN_LEFT;    // DPad Left
    if (wb & 0x0008) b |= DM_BTN_RIGHT;   // DPad Right
    if (xs.Gamepad.bLeftTrigger > 30) b |= DM_BTN_ZL;
    if (xs.Gamepad.bRightTrigger > 30) b |= DM_BTN_ZR;

    if (out_btn) *out_btn = b;

    // Normalization & deadzones:
    const int DEADZONE_L = 7849;
    const int DEADZONE_R = 8689;
    int lx_raw = xs.Gamepad.sThumbLX, ly_raw = xs.Gamepad.sThumbLY;
    int rx_raw = xs.Gamepad.sThumbRX, ry_raw = xs.Gamepad.sThumbRY;

    if (abs(lx_raw) > DEADZONE_L && out_lx) *out_lx = (int32_t)((int64_t)lx_raw * DM_STICK_MAX / 32767);
    if (abs(ly_raw) > DEADZONE_L && out_ly) *out_ly = (int32_t)((int64_t)ly_raw * DM_STICK_MAX / 32767);
    if (abs(rx_raw) > DEADZONE_R && out_rx) *out_rx = (int32_t)((int64_t)rx_raw * DM_STICK_MAX / 32767);
    if (abs(ry_raw) > DEADZONE_R && out_ry) *out_ry = (int32_t)((int64_t)ry_raw * DM_STICK_MAX / 32767);
}

static void push_input(void) {
    uint64_t xbtn = 0;
    int32_t xlx = 0, xly = 0, xrx = 0, xry = 0;
    poll_xinput(&xbtn, &xlx, &xly, &xrx, &xry);

    int32_t klx = (g_d - g_a) * DM_STICK_MAX;
    int32_t kly = (g_w - g_s) * DM_STICK_MAX;

    int32_t lx = klx ? klx : xlx;
    int32_t ly = kly ? kly : xly;
    dm_input_set(g_btn | xbtn, lx, ly, xrx, xry);
}

static uint64_t vk_to_btn(WPARAM vk) {
    switch (vk) {
        case 'J': return DM_BTN_A;
        case 'K': return DM_BTN_B;
        case 'U': return DM_BTN_X;
        case 'I': return DM_BTN_Y;
        case 'F': return DM_BTN_LS;
        case 'G': return DM_BTN_RS;
        case 'Q': return DM_BTN_L;
        case 'E': return DM_BTN_R;
        case 'Z': return DM_BTN_ZL;
        case 'C': return DM_BTN_ZR;
        case VK_RETURN: return DM_BTN_PLUS;
        case VK_TAB: return DM_BTN_MINUS;
        case VK_LEFT: return DM_BTN_LEFT;
        case VK_UP: return DM_BTN_UP;
        case VK_RIGHT: return DM_BTN_RIGHT;
        case VK_DOWN: return DM_BTN_DOWN;
        case VK_SPACE: return DM_BTN_SHAKE;
        default: return 0;
    }
}

static void on_key_win(WPARAM vk, int down) {
    if (!g_input_on) return;
    switch (vk) {
        case 'W': g_w = down; break;
        case 'A': g_a = down; break;
        case 'S': g_s = down; break;
        case 'D': g_d = down; break;
    }
    uint64_t m = vk_to_btn(vk);
    if (m) g_btn = down ? (g_btn | m) : (g_btn & ~m);
    push_input();
}

static void input_release_win(void) {
    g_input_on = 0;
    g_btn = 0; g_w = g_a = g_s = g_d = 0;
    push_input();
    if (g_mouse_down) {
        g_mouse_down = 0;
        dm_touch_set(NULL, 0);
    }
}

static void on_mouse_win(HWND hwnd, int x, int y, int down) {
    if (!down) {
        if (g_mouse_down) {
            g_mouse_down = 0;
            dm_touch_set(NULL, 0);
        }
        return;
    }
    if (!g_input_on) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    double cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;
    double w = cw, h = ch;
    if (w / h > 16.0 / 9.0) w = h * 16.0 / 9.0; else h = w * 9.0 / 16.0;
    double x0 = (cw - w) / 2.0, y0 = (ch - h) / 2.0;
    dm_touch t = {
        0,
        (float)fmin(fmax((x - x0) / w, 0.0), 0.999),
        (float)fmin(fmax((y - y0) / h, 0.0), 0.999)
    };
    dm_touch_set(&t, 1);
    g_mouse_down = 1;
}

static int save_bmp(const char *path, const uint32_t *bgra, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t row_bytes = (uint32_t)w * 4;
    uint32_t img_bytes = row_bytes * (uint32_t)h;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t file_sz = 54 + img_bytes;
    memcpy(hdr + 2, &file_sz, 4);
    uint32_t off = 54;
    memcpy(hdr + 10, &off, 4);
    uint32_t dib_sz = 40;
    memcpy(hdr + 14, &dib_sz, 4);
    int32_t bw = w, bh = -h; // top-down DIB
    memcpy(hdr + 18, &bw, 4);
    memcpy(hdr + 22, &bh, 4);
    uint16_t planes = 1, bpp = 32;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &img_bytes, 4);
    fwrite(hdr, 1, 54, f);
    fwrite(bgra, 1, img_bytes, f);
    fclose(f);
    return 1;
}

void front_win_present_bgra(const uint32_t *bgra, int w, int h, uint64_t frame, uint64_t submits, uint64_t draws, uint64_t clears, uint32_t last_clear_bgra) {
    pthread_mutex_lock(&g_fb_mu);
    if (w > 0 && h > 0 && bgra) {
        if (w != g_fb_w || h != g_fb_h || !g_fb_pixels) {
            free(g_fb_pixels);
            g_fb_pixels = malloc((size_t)w * (size_t)h * 4);
            g_fb_w = g_fb_pixels ? w : 0;
            g_fb_h = g_fb_pixels ? h : 0;
        }
        if (g_fb_pixels) memcpy(g_fb_pixels, bgra, (size_t)w * (size_t)h * 4);
    }
    g_fb_frame = frame;
    g_fb_submits = submits;
    g_fb_draws = draws;
    g_fb_clears = clears;
    if (last_clear_bgra) g_fb_clear_bgra = last_clear_bgra;
    pthread_mutex_unlock(&g_fb_mu);

    if (vk_win_is_active()) {
        if (bgra && w > 0 && h > 0) {
            vk_win_present(bgra, w, h);
        } else {
            uint32_t c = g_fb_clear_bgra;
            vk_win_present(&c, 1, 1);
        }
        void *vk_pix = NULL; int vkw = 0, vkh = 0;
        if (vk_win_get_pixels(&vk_pix, &vkw, &vkh) && vk_pix) {
            pthread_mutex_lock(&g_fb_mu);
            if (vkw != g_fb_w || vkh != g_fb_h || !g_fb_pixels) {
                free(g_fb_pixels);
                g_fb_pixels = malloc((size_t)vkw * (size_t)vkh * 4);
                g_fb_w = g_fb_pixels ? vkw : 0;
                g_fb_h = g_fb_pixels ? vkh : 0;
            }
            if (g_fb_pixels) memcpy(g_fb_pixels, vk_pix, (size_t)vkw * (size_t)vkh * 4);
            pthread_mutex_unlock(&g_fb_mu);
        }
    } else if (g_hwnd) {
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

// Capture the entire client area to a BMP file
int front_win_capture_bmp(const char *path) {
    pthread_mutex_lock(&g_fb_mu);
    int has_fb_data = 0;
    if (g_fb_pixels && g_fb_w > 0 && g_fb_h > 0) {
        size_t total = (size_t)g_fb_w * g_fb_h;
        size_t step = total / 1000 + 1;
        for (size_t i = 0; i < total; i += step) {
            if (g_fb_pixels[i] & 0x00FFFFFFu) {
                has_fb_data = 1;
                break;
            }
        }
    }
    if (has_fb_data || (vk_win_is_active() && g_fb_pixels && g_fb_w > 0 && g_fb_h > 0)) {
        int ok = save_bmp(path, g_fb_pixels, g_fb_w, g_fb_h);
        pthread_mutex_unlock(&g_fb_mu);
        return ok;
    }
    pthread_mutex_unlock(&g_fb_mu);

    if (!g_hwnd) return 0;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) cw = 1280, ch = 720;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cw;
    bmi.bmiHeader.biHeight = -ch;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    int ok = 0;
    if (hbm && bits) {
        HGDIOBJ old = SelectObject(hdcMem, hbm);
        POINT pt = { 0, 0 };
        ClientToScreen(g_hwnd, &pt);
        BitBlt(hdcMem, 0, 0, cw, ch, hdcScreen, pt.x, pt.y, SRCCOPY);
        GdiFlush();
        uint32_t *px = (uint32_t *)bits;
        for (int i = 0; i < cw * ch; i++) px[i] |= 0xFF000000u;
        ok = save_bmp(path, px, cw, ch);
        SelectObject(hdcMem, old);
        DeleteObject(hbm);
    }
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return ok;
}

static void paint_client(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ oldBM = SelectObject(memDC, memBM);

    pthread_mutex_lock(&g_fb_mu);
    uint64_t frame = g_fb_frame, submits = g_fb_submits, draws = g_fb_draws, clears = g_fb_clears;
    uint32_t cbgra = g_fb_clear_bgra;
    if (g_fb_pixels && g_fb_w > 0 && g_fb_h > 0) {
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof bmi);
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_fb_w;
        bmi.bmiHeader.biHeight = -g_fb_h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(memDC, HALFTONE);
        StretchDIBits(memDC, 0, 0, cw, ch, 0, 0, g_fb_w, g_fb_h, g_fb_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        HBRUSH br = CreateSolidBrush(RGB((cbgra >> 16) & 0xFF, (cbgra >> 8) & 0xFF, cbgra & 0xFF));
        FillRect(memDC, &rc, br);
        DeleteObject(br);
    }
    pthread_mutex_unlock(&g_fb_mu);

    // Draw subtle HUD status banner at top-left so live progress is always visible
    if (!getenv("DM_HIDE_HUD")) {
        RECT hudRc = { 16, 16, 740, 96 };
        HBRUSH hudBr = CreateSolidBrush(RGB(16, 20, 28));
        FillRect(memDC, &hudRc, hudBr);
        DeleteObject(hudBr);

        SetBkMode(memDC, TRANSPARENT);
        HFONT hFont = CreateFontA(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        HGDIOBJ oldFont = SelectObject(memDC, hFont);

        char line1[256], line2[256];
        snprintf(line1, sizeof line1, "nxnative Windows ARM64 | %s (%s)", DM_GAME_NAME, DM_GAME_ID);
        snprintf(line2, sizeof line2, "NVN Frame: %llu | Submits: %llu | Draws: %llu | Clears: %llu",
                 (unsigned long long)frame, (unsigned long long)submits,
                 (unsigned long long)draws, (unsigned long long)clears);

        SetTextColor(memDC, RGB(110, 220, 255));
        TextOutA(memDC, 28, 26, line1, (int)strlen(line1));
        SetTextColor(memDC, RGB(230, 235, 245));
        TextOutA(memDC, 28, 54, line2, (int)strlen(line2));

        SelectObject(memDC, oldFont);
        DeleteObject(hFont);
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            exit(0);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                input_release_win();
                if (!IsIconic(hwnd)) dm_focus_set(2);
            } else {
                g_input_on = 1;
                dm_focus_set(1);
            }
            return 0;
        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) {
                input_release_win();
                dm_focus_set(3);
                dm_audio_suspend(1);
                dm_gfx_suspend(1);
            } else if (wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED) {
                dm_gfx_suspend(0);
                dm_audio_suspend(0);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(lp & (1 << 30))) on_key_win(wp, 1);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            on_key_win(wp, 0);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            on_mouse_win(hwnd, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1);
            return 0;
        case WM_MOUSEMOVE:
            if (wp & MK_LBUTTON)
                on_mouse_win(hwnd, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            on_mouse_win(hwnd, 0, 0, 0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PRINTCLIENT:
            paint_client(hwnd, (HDC)wp);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!vk_win_is_active()) paint_client(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                static int timer_ticks = 0;
                timer_ticks++;

                static int auto_a_enabled = -1;
                if (auto_a_enabled < 0) {
                    const char *aa = getenv("DM_AUTO_A");
                    auto_a_enabled = (aa && !atoi(aa)) ? 0 : 1;
                }
                if (auto_a_enabled) {
                    // Periodic 'A' button tap to advance language and intro screens:
                    // Every 40 ticks (~640 ms), hold 'A' for 8 ticks (~130 ms) then release
                    int cycle = timer_ticks % 40;
                    if (timer_ticks >= 60 && cycle < 8) {
                        g_btn |= DM_BTN_A;
                    } else if (timer_ticks >= 60) {
                        g_btn &= ~DM_BTN_A;
                    }
                }

                push_input();
                if ((timer_ticks % 15) == 0) {
                    char title[256];
                    pthread_mutex_lock(&g_fb_mu);
                    uint64_t f = g_fb_frame, s = g_fb_submits, d = g_fb_draws;
                    pthread_mutex_unlock(&g_fb_mu);
                    snprintf(title, sizeof title, "%s - Frame %llu (Submits: %llu, Draws: %llu)",
                             DM_GAME_NAME, (unsigned long long)f, (unsigned long long)s, (unsigned long long)d);
                    SetWindowTextA(hwnd, title);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                const char *as = getenv("DM_WIN_AUTOSHOT");
                if (as && *as && (timer_ticks == 60 || (timer_ticks % 180) == 0)) {
                    front_win_capture_bmp(as);
                }
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

typedef struct { void (*fn)(void); } guest_arg;
static void *guest_tramp(void *p) {
    ((guest_arg *)p)->fn();
    LOG("guest thread finished");
    exit(0);
    return NULL;
}

void front_run(void (*guest)(void)) {
    const char *tl = getenv("DM_TIME_LIMIT");
    if (tl && atoi(tl) > 0) {
        LOG("DM_TIME_LIMIT=%d s armed at startup", atoi(tl));
        alarm((unsigned)atoi(tl));
    }

    const char *hl = getenv("DM_HEADLESS");
    if (hl && atoi(hl)) {
        guest();
        return;
    }

    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "DmHostWindowClass";
    RegisterClassExA(&wc);

    RECT r = { 0, 0, 1280, 720 };
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    AdjustWindowRect(&r, style, FALSE);

    g_hwnd = CreateWindowExA(
        0, "DmHostWindowClass", DM_GAME_NAME, style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        NULL, NULL, hInst, NULL
    );
    if (!g_hwnd) {
        LOG("[RED] CreateWindowExA failed (%lu), running headless", GetLastError());
        guest();
        return;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    UpdateWindow(g_hwnd);
    SetTimer(g_hwnd, 1, 16, NULL);
    LOG("front end: opened 1280x720 Win32 window for %s", DM_GAME_NAME);

    const char *no_vk = getenv("DM_NO_VK");
    if (!no_vk || !atoi(no_vk)) {
        vk_win_init(g_hwnd, 1280, 720);
    }

    static guest_arg ga;
    ga.fn = guest;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 64u << 20);
    pthread_t th;
    if (pthread_create(&th, &a, guest_tramp, &ga)) die("pthread_create guest");

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    vk_win_shutdown();
}

void *front_metal_layer(void) {
    return (void *)g_hwnd;
}

int front_screen_max_fps(void) {
    return 60;
}

void front_screen_match(int fps) {
    (void)fps;
}

void front_progress(const char *title, long done, long total) {
    (void)title; (void)done; (void)total;
}

int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    (void)title; (void)sub; (void)guide; (void)initial; (void)max_chars; (void)out; (void)out_cap;
    return -1;
}
