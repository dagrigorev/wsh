/*
 * main.cpp — Wsh entry point with internal terminal panes.
 *
 * Pass 4: terminal pane splitting.  zsh itself does not own panes; real panes
 * belong to terminal multiplexers.  WSH implements the same user-facing concept
 * directly in the terminal layer: each pane owns an independent ScreenBuffer,
 * VT parser and either a built-in WSH REPL or a ConPTY child.
 */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <stdio.h>
#include <exception>

#include "core/str_util.h"
#include "core/log.h"
#include "core/path_util.h"
#include "core/unicode.h"
#include "platform/config.h"
#include "platform/pty.h"
#include "platform/input.h"
#include "terminal/screen.h"
#include "terminal/vt_parser.h"
#include "terminal/renderer.h"
#include "shell/shell_ctx.h"
#include "shell/history.h"
#include "shell/completion.h"
#include "window.h"
#include "repl.h"

#define WSH_MAX_PANES 4

typedef struct TerminalIO TerminalIO;
typedef struct TerminalPane TerminalPane;

struct TerminalIO {
    IShellIO  base;
    HWND      hwnd;
    VtParser *vt;
    CRITICAL_SECTION *lock;
};

struct TerminalPane {
    ScreenBuffer screen;
    VtParser     vt;
    ShellContext shell;
    Repl         repl;
    PtySession   pty;
    TerminalIO   io;
    RECT         rect;
    bool         initialized;
    bool         use_pty;
    int          wheel_accum;
};

static HWND              g_hwnd     = NULL;
static Config            g_cfg      = {0};
static Renderer          g_renderer = {0};
static TerminalPane      g_panes[WSH_MAX_PANES];
static int               g_pane_count = 0;
static int               g_active_pane = 0;
static CRITICAL_SECTION  g_lock;
static bool              g_suppress_char = false;

static void update_native_scrollbar(HWND hwnd);
static void layout_panes(HWND hwnd);

static TerminalPane *active_pane(void) {
    if (g_active_pane < 0) g_active_pane = 0;
    if (g_active_pane >= g_pane_count) g_active_pane = g_pane_count - 1;
    return g_pane_count > 0 ? &g_panes[g_active_pane] : NULL;
}

static void terminal_write(IShellIO *self, const char *buf, int len) {
    TerminalIO *t = (TerminalIO *)self;
    EnterCriticalSection(t->lock);
    vt_parser_feed(t->vt, buf, len);
    LeaveCriticalSection(t->lock);
    update_native_scrollbar(t->hwnd);
    InvalidateRect(t->hwnd, NULL, FALSE);
}

static int terminal_read_line(IShellIO *self, char *buf, int size) {
    (void)self;
    if (!buf || size <= 0) return 0;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n = 0;
    ReadFile(hin, buf, (DWORD)(size - 1), &n, NULL);
    buf[n] = '\0';
    return (int)n;
}

static void get_exe_dir(char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    GetModuleFileNameA(NULL, out, (DWORD)out_size);
    char *last_bs = strrchr(out, '\\');
    if (last_bs) *last_bs = '\0';
}

static void ensure_user_zshrc(void) {
    char zshrc[MAX_PATH] = {0};
    char profile[MAX_PATH] = {0};
    GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
    _snprintf(zshrc, MAX_PATH, "%s\\.zshrc", profile);

    char default_zshrc[MAX_PATH];
    char exe_dir[MAX_PATH] = {0};
    get_exe_dir(exe_dir, MAX_PATH);
    _snprintf(default_zshrc, MAX_PATH, "%s\\config\\.zshrc", exe_dir);
    if (!path_exists(zshrc) && path_exists(default_zshrc)) {
        wchar_t *wsrc = u8_to_u16(default_zshrc, NULL);
        wchar_t *wdst = u8_to_u16(zshrc, NULL);
        if (wsrc && wdst && CopyFileW(wsrc, wdst, TRUE)) {
            WSH_LOG_INFO("Installed default ~/.zshrc");
        } else {
            WSH_LOG_WARN("Failed to install default ~/.zshrc");
        }
        str_free(wsrc);
        str_free(wdst);
    }
}

static void source_user_zshrc(ShellContext *shell) {
    char zshrc[MAX_PATH] = {0};
    char profile[MAX_PATH] = {0};
    GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
    _snprintf(zshrc, MAX_PATH, "%s\\.zshrc", profile);
    if (path_exists(zshrc)) shell_source(shell, zshrc);
}

static void pane_grid_from_rect(const RECT *rc, int *cols, int *rows) {
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    int c = (int)((w - g_renderer.padding_x * 2) / g_renderer.cell_w);
    int r = (int)((h - g_renderer.padding_y * 2) / g_renderer.cell_h);
    if (c < 10) c = 10;
    if (r < 4) r = 4;
    *cols = c;
    *rows = r;
}

static void on_title(const char *title, void *ud) {
    (void)ud;
    wchar_t *w = u8_to_u16(title, NULL);
    if (w) { SetWindowTextW(g_hwnd, w); str_free(w); }
}

static void on_pty_data(const char *buf, int len, void *ud) {
    TerminalPane *pane = (TerminalPane *)ud;
    if (!pane || !pane->initialized) return;
    EnterCriticalSection(&g_lock);
    vt_parser_feed(&pane->vt, buf, len);
    LeaveCriticalSection(&g_lock);
    update_native_scrollbar(g_hwnd);
    InvalidateRect(g_hwnd, NULL, FALSE);
    PostMessage(g_hwnd, WM_USER, 0, 0);
}

static bool pane_start(TerminalPane *pane) {
    if (!pane) return false;

    /* Preserve the target pane rectangle before clearing the pane object.
       The previous implementation wiped pane->rect and temporarily created
       every new pane as 800x600. That was visually harmless after the next
       layout pass, but unsafe for prompt/output emitted during initialization. */
    RECT initial_rect = pane->rect;
    if (initial_rect.right <= initial_rect.left || initial_rect.bottom <= initial_rect.top) {
        GetClientRect(g_hwnd, &initial_rect);
        if (initial_rect.right <= initial_rect.left || initial_rect.bottom <= initial_rect.top) {
            initial_rect.left = 0;
            initial_rect.top = 0;
            initial_rect.right = 800;
            initial_rect.bottom = 600;
        }
    }

    memset(pane, 0, sizeof(*pane));
    pane->rect = initial_rect;

    int cols = 80, rows = 24;
    pane_grid_from_rect(&pane->rect, &cols, &rows);
    screen_init(&pane->screen, cols, rows, g_cfg.general.scrollback);

    vt_parser_init(&pane->vt, &pane->screen);
    pane->vt.on_title = on_title;
    pane->vt.userdata = NULL;

    pane->io.base.write     = terminal_write;
    pane->io.base.read_line = terminal_read_line;
    pane->io.hwnd           = g_hwnd;
    pane->io.vt             = &pane->vt;
    pane->io.lock           = &g_lock;

    shell_ctx_init(&pane->shell, (IShellIO *)&pane->io);
    source_user_zshrc(&pane->shell);
    repl_init(&pane->repl, &pane->shell);

    pane->initialized = true;
    if (strcmp(g_cfg.general.shell, "wsh") == 0 || g_cfg.general.shell[0] == '\0') {
        pane->use_pty = false;
        repl_show_prompt(&pane->repl);
    } else {
        pane->use_pty = true;
        if (!pty_create(&pane->pty, cols, rows)) {
            WSH_LOG_WARN("pane pty_create failed — using built-in shell");
            pane->use_pty = false;
        } else {
            wchar_t *wcmd = u8_to_u16(g_cfg.general.shell, NULL);
            if (!pty_spawn(&pane->pty, wcmd ? wcmd : L"cmd.exe", NULL, on_pty_data, pane)) {
                WSH_LOG_ERROR("pane pty_spawn failed");
                pty_close(&pane->pty);
                pane->use_pty = false;
            }
            str_free(wcmd);
        }
        if (!pane->use_pty) repl_show_prompt(&pane->repl);
    }
    return true;
}

static void pane_stop(TerminalPane *pane) {
    if (!pane || !pane->initialized) return;
    if (pane->use_pty) pty_close(&pane->pty);
    repl_free(&pane->repl);
    shell_ctx_free(&pane->shell);
    screen_free(&pane->screen);
    memset(pane, 0, sizeof(*pane));
}

static void pane_resize(TerminalPane *pane) {
    if (!pane || !pane->initialized) return;
    int cols = 80, rows = 24;
    pane_grid_from_rect(&pane->rect, &cols, &rows);
    screen_resize(&pane->screen, cols, rows);
    if (pane->use_pty) pty_resize(&pane->pty, cols, rows);
}

static void layout_panes(HWND hwnd) {
    if (!hwnd || g_pane_count <= 0) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    RECT r[WSH_MAX_PANES] = {0};
    if (g_pane_count == 1) {
        r[0] = rc;
    } else if (g_pane_count == 2) {
        int mid = rc.left + w / 2;
        r[0] = {rc.left, rc.top, mid, rc.bottom};
        r[1] = {mid, rc.top, rc.right, rc.bottom};
    } else if (g_pane_count == 3) {
        int midx = rc.left + w / 2;
        int midy = rc.top + h / 2;
        r[0] = {rc.left, rc.top, midx, rc.bottom};
        r[1] = {midx, rc.top, rc.right, midy};
        r[2] = {midx, midy, rc.right, rc.bottom};
    } else {
        int midx = rc.left + w / 2;
        int midy = rc.top + h / 2;
        r[0] = {rc.left, rc.top, midx, midy};
        r[1] = {midx, rc.top, rc.right, midy};
        r[2] = {rc.left, midy, midx, rc.bottom};
        r[3] = {midx, midy, rc.right, rc.bottom};
    }

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_pane_count; ++i) {
        g_panes[i].rect = r[i];
        pane_resize(&g_panes[i]);
    }
    LeaveCriticalSection(&g_lock);
    update_native_scrollbar(hwnd);
}

static int pane_hit_test(int x, int y) {
    POINT pt = {x, y};
    for (int i = 0; i < g_pane_count; ++i) {
        if (PtInRect(&g_panes[i].rect, pt)) return i;
    }
    return g_active_pane;
}

static bool pane_add(void) {
    if (g_pane_count >= WSH_MAX_PANES) return false;
    int idx = g_pane_count++;
    memset(&g_panes[idx], 0, sizeof(g_panes[idx]));
    g_panes[idx].rect = g_panes[g_active_pane].rect;
    if (!pane_start(&g_panes[idx])) {
        memset(&g_panes[idx], 0, sizeof(g_panes[idx]));
        g_pane_count--;
        return false;
    }
    g_active_pane = idx;
    layout_panes(g_hwnd);
    return true;
}

static void pane_close_active(void) {
    if (g_pane_count <= 1) return;

    /* Keep this operation pointer-safe for ConPTY callbacks.  Reader callbacks
       store the address of their TerminalPane as userdata, so panes must not be
       memmoved while a child process is alive.  For this first pane pass we close
       the last physical pane; when the focused pane is not last, focus is moved
       to the last pane before closing. */
    int idx = g_active_pane;
    if (idx != g_pane_count - 1) {
        g_active_pane = g_pane_count - 1;
        idx = g_active_pane;
    }

    pane_stop(&g_panes[idx]);
    memset(&g_panes[idx], 0, sizeof(g_panes[idx]));
    g_pane_count--;
    if (g_active_pane >= g_pane_count) g_active_pane = g_pane_count - 1;
    layout_panes(g_hwnd);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void update_native_scrollbar(HWND hwnd) {
    if (!hwnd) return;
    TerminalPane *pane = active_pane();
    if (!pane || !pane->initialized) return;

    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;

    EnterCriticalSection(&g_lock);
    int max_offset = screen_max_viewport_offset(&pane->screen);
    int offset     = pane->screen.viewport_offset;
    if (offset < 0) offset = 0;
    if (offset > max_offset) offset = max_offset;
    bool alt = pane->screen.alt_screen_active;
    LeaveCriticalSection(&g_lock);

    if (alt || max_offset <= 0) {
        si.nMin = si.nMax = si.nPos = 0; si.nPage = 1;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        ShowScrollBar(hwnd, SB_VERT, FALSE);
        return;
    }

    si.nMin  = 0;
    si.nMax  = max_offset;
    si.nPage = 1;
    si.nPos  = max_offset - offset;
    ShowScrollBar(hwnd, SB_VERT, TRUE);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

static void expand_title_template(const Config *cfg, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    const char *tmpl = (cfg && cfg->general.title[0]) ? cfg->general.title : "Wsh - ${cwd}";
    char cwd[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    int oi = 0;
    for (const char *p = tmpl; *p && oi < out_size - 1; ) {
        if (strncmp(p, "${cwd}", 6) == 0) { for (const char *c = cwd; *c && oi < out_size - 1; c++) out[oi++] = *c; p += 6; }
        else if (strncmp(p, "${theme}", 8) == 0) { const char *theme = (cfg && cfg->general.theme[0]) ? cfg->general.theme : "default"; for (const char *c = theme; *c && oi < out_size - 1; c++) out[oi++] = *c; p += 8; }
        else if (strncmp(p, "${version}", 10) == 0) { const char *ver = WSH_VERSION; for (const char *c = ver; *c && oi < out_size - 1; c++) out[oi++] = *c; p += 10; }
        else out[oi++] = *p++;
    }
    out[oi] = '\0';
}

static void apply_configured_theme(Config *cfg) {
    if (!cfg || !cfg->general.theme[0]) return;
    if (strchr(cfg->general.theme, '\\') || strchr(cfg->general.theme, '/') || strchr(cfg->general.theme, ':')) {
        if (!config_apply_theme_file(cfg, cfg->general.theme)) WSH_LOG_WARN("Theme file not found: %s", cfg->general.theme);
        return;
    }
    char exe_dir[MAX_PATH]; get_exe_dir(exe_dir, MAX_PATH);
    char theme_path[MAX_PATH];
    _snprintf(theme_path, MAX_PATH, "%s\\themes\\%s.toml", exe_dir, cfg->general.theme);
    if (config_apply_theme_file(cfg, theme_path)) return;
    char appdata[MAX_PATH] = {0};
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    _snprintf(theme_path, MAX_PATH, "%s\\Wsh\\themes\\%s.toml", appdata, cfg->general.theme);
    if (!config_apply_theme_file(cfg, theme_path)) WSH_LOG_WARN("Configured theme was not found: %s", cfg->general.theme);
}

static void apply_window_title(HWND hwnd, const Config *cfg) {
    char title[512];
    expand_title_template(cfg, title, (int)sizeof(title));
    wchar_t *wtitle = u8_to_u16(title, NULL);
    if (wtitle) { SetWindowTextW(hwnd, wtitle); str_free(wtitle); }
}

static void pane_pixel_to_cell(const TerminalPane *pane, int px, int py, int *col, int *row) {
    int c = (int)(((float)(px - pane->rect.left - g_renderer.padding_x)) / g_renderer.cell_w);
    int r = (int)(((float)(py - pane->rect.top  - g_renderer.padding_y)) / g_renderer.cell_h);
    if (c < 0) c = 0; if (r < 0) r = 0;
    if (c >= pane->screen.cols) c = pane->screen.cols - 1;
    if (r >= pane->screen.rows) r = pane->screen.rows - 1;
    *col = c; *row = r;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            update_native_scrollbar(hwnd);
            EnterCriticalSection(&g_lock);
            renderer_begin_frame(&g_renderer);
            for (int i = 0; i < g_pane_count; ++i) {
                TerminalPane *p = &g_panes[i];
                bool active = (i == g_active_pane);
                bool cursor_visible_in_view = (p->screen.viewport_offset == 0);
                renderer_paint_region(&g_renderer, &p->screen, &p->rect, active,
                                      cursor_visible_in_view, p->screen.cursor_x, p->screen.cursor_y);
            }
            renderer_end_frame(&g_renderer);
            LeaveCriticalSection(&g_lock);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (w > 0 && h > 0) {
                renderer_resize(&g_renderer, w, h);
                layout_panes(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            renderer_update_dpi(&g_renderer, (float)HIWORD(wParam));
            const RECT *rc = (const RECT *)lParam;
            SetWindowPos(hwnd, NULL, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout_panes(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_TIMER:
            if (wParam == 1) { renderer_toggle_cursor_blink(&g_renderer); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;

        case WM_SETFOCUS: { TerminalPane *p = active_pane(); if (p) p->screen.cursor_visible = true; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        case WM_KILLFOCUS: { TerminalPane *p = active_pane(); if (p) p->screen.cursor_visible = false; InvalidateRect(hwnd, NULL, FALSE); return 0; }

        case WM_KEYDOWN: {
            TerminalPane *p = active_pane();
            if (!p) return 0;
            bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

            if (ctrl && shift && wParam == 'D') { g_suppress_char = true; pane_add(); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (ctrl && shift && wParam == 'W') { g_suppress_char = true; pane_close_active(); return 0; }
            if (alt && (wParam == VK_LEFT || wParam == VK_UP)) { g_suppress_char = true; if (g_pane_count > 1) { g_active_pane = (g_active_pane + g_pane_count - 1) % g_pane_count; update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); } return 0; }
            if (alt && (wParam == VK_RIGHT || wParam == VK_DOWN)) { g_suppress_char = true; if (g_pane_count > 1) { g_active_pane = (g_active_pane + 1) % g_pane_count; update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); } return 0; }

            InputEvent ev = input_translate(wParam, 0, lParam, p->screen.app_cursor_keys);
            g_suppress_char = input_suppress_char(wParam, lParam);

            switch (ev.action) {
                case INPUT_COPY:
                    break;
                case INPUT_PASTE:
                    if (OpenClipboard(hwnd)) {
                        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                        if (hd) {
                            const wchar_t *wtext = (const wchar_t *)GlobalLock(hd);
                            if (wtext) {
                                int u8len = 0;
                                char *text = wsh_utf16_to_utf8_clipboard(wtext, &u8len);
                                if (text) { if (p->use_pty) pty_write(&p->pty, text, u8len); else repl_handle_input(&p->repl, text, u8len); str_free(text); }
                                GlobalUnlock(hd);
                            }
                        }
                        CloseClipboard();
                    }
                    break;
                case INPUT_SCROLL_UP:
                    EnterCriticalSection(&g_lock); screen_scroll_viewport(&p->screen, 3); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_SCROLL_DOWN:
                    EnterCriticalSection(&g_lock); screen_scroll_viewport(&p->screen, -3); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_SCROLL_PAGE_UP:
                    EnterCriticalSection(&g_lock); screen_page_viewport(&p->screen, 1); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_SCROLL_PAGE_DOWN:
                    EnterCriticalSection(&g_lock); screen_page_viewport(&p->screen, -1); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_ZOOM_IN:
                    renderer_set_font(&g_renderer, g_cfg.font.family, g_renderer.font.pt_size + 1.0f); layout_panes(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_ZOOM_OUT:
                    if (g_renderer.font.pt_size > 6.0f) renderer_set_font(&g_renderer, g_cfg.font.family, g_renderer.font.pt_size - 1.0f); layout_panes(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case INPUT_CHAR:
                    if (ev.len > 0) {
                        if (p->screen.viewport_offset) { EnterCriticalSection(&g_lock); p->screen.viewport_offset = 0; screen_mark_dirty_all(&p->screen); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); }
                        if (p->use_pty) pty_write(&p->pty, ev.bytes, ev.len);
                        else if (!repl_handle_input(&p->repl, ev.bytes, ev.len)) PostQuitMessage(0);
                    }
                    break;
                default: break;
            }
            return 0;
        }

        case WM_CHAR: {
            TerminalPane *p = active_pane();
            if (!p) return 0;
            if (g_suppress_char) { g_suppress_char = false; return 0; }
            WCHAR ch = (WCHAR)wParam;
            if (ch == '\r') return 0;
            InputEvent ev = input_translate(0, ch, lParam, p->screen.app_cursor_keys);
            if (ev.action == INPUT_CHAR && ev.len > 0) {
                if (p->screen.viewport_offset) { EnterCriticalSection(&g_lock); p->screen.viewport_offset = 0; screen_mark_dirty_all(&p->screen); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); }
                if (p->use_pty) pty_write(&p->pty, ev.bytes, ev.len);
                else if (!repl_handle_input(&p->repl, ev.bytes, ev.len)) PostQuitMessage(0);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            int hit = pane_hit_test(mx, my);
            if (hit >= 0 && hit < g_pane_count) g_active_pane = hit;
            TerminalPane *p = active_pane();
            int col, row; pane_pixel_to_cell(p, mx, my, &col, &row);
            g_renderer.sel_start_col = g_renderer.sel_end_col = col;
            g_renderer.sel_start_row = g_renderer.sel_end_row = row;
            g_renderer.sel_active = true; g_renderer.sel_valid = false;
            update_native_scrollbar(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_renderer.sel_active) {
                TerminalPane *p = active_pane();
                int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
                int col, row; pane_pixel_to_cell(p, mx, my, &col, &row);
                g_renderer.sel_end_col = col; g_renderer.sel_end_row = row;
                g_renderer.sel_valid = (col != g_renderer.sel_start_col || row != g_renderer.sel_start_row);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            g_renderer.sel_active = false;
            TerminalPane *p = active_pane();
            if (p && g_renderer.sel_valid && OpenClipboard(hwnd)) {
                int sr = g_renderer.sel_start_row, sc = g_renderer.sel_start_col;
                int er = g_renderer.sel_end_row,   ec = g_renderer.sel_end_col;
                if (sr > er || (sr == er && sc > ec)) { int tr=sr,tc=sc; sr=er;sc=ec; er=tr;ec=tc; }
                char text[65536]; int ti = 0;
                EnterCriticalSection(&g_lock);
                for (int r = sr; r <= er && ti < 65520; r++) {
                    int c0 = (r == sr) ? sc : 0;
                    int c1 = (r == er) ? ec : p->screen.cols - 1;
                    int last_ns = c0 - 1;
                    for (int c = c0; c <= c1; c++) {
                        const ScreenCell *cell = screen_visible_cell(&p->screen, r, c);
                        if (cell && !cell->wide_cont && cell->ch > ' ') last_ns = c;
                    }
                    for (int c = c0; c <= last_ns && ti < 65512; c++) {
                        const ScreenCell *cell = screen_visible_cell(&p->screen, r, c);
                        if (cell && cell->wide_cont) continue;
                        uint32_t ch = cell ? cell->ch : ' '; if (!ch) ch = ' ';
                        char enc[4]; int n = utf8_encode(ch, enc);
                        if (ti + n >= 65512) break;
                        memcpy(text + ti, enc, (size_t)n); ti += n;
                    }
                    if (r < er) { text[ti++] = '\r'; text[ti++] = '\n'; }
                }
                text[ti] = '\0';
                LeaveCriticalSection(&g_lock);
                int wchars = 0; wchar_t *wtext = wsh_utf8_to_utf16_clipboard(text, ti, &wchars);
                if (wtext) {
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wchars + 1) * sizeof(wchar_t));
                    if (hg) { memcpy(GlobalLock(hg), wtext, ((size_t)wchars + 1) * sizeof(wchar_t)); GlobalUnlock(hg); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, hg); }
                    str_free(wtext);
                }
                CloseClipboard();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            TerminalPane *p = active_pane();
            if (!p) return 0;
            if (OpenClipboard(hwnd)) {
                HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                if (hd) {
                    const wchar_t *wtxt = (const wchar_t *)GlobalLock(hd);
                    if (wtxt) {
                        int u8len = 0; char *txt = wsh_utf16_to_utf8_clipboard(wtxt, &u8len);
                        if (txt) { if (p->use_pty) pty_write(&p->pty, txt, u8len); else repl_handle_input(&p->repl, txt, u8len); str_free(txt); }
                        GlobalUnlock(hd);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            TerminalPane *p = active_pane(); if (!p) return 0;
            p->wheel_accum += GET_WHEEL_DELTA_WPARAM(wParam);
            int steps = p->wheel_accum / WHEEL_DELTA;
            p->wheel_accum %= WHEEL_DELTA;
            if (steps != 0) { EnterCriticalSection(&g_lock); screen_scroll_viewport(&p->screen, steps * 3); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        }

        case WM_VSCROLL: {
            TerminalPane *p = active_pane(); if (!p) return 0;
            EnterCriticalSection(&g_lock);
            int max_offset = screen_max_viewport_offset(&p->screen);
            int pos = max_offset - p->screen.viewport_offset;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= 1; break;
                case SB_LINEDOWN: pos += 1; break;
                case SB_PAGEUP: pos -= (p->screen.rows > 1 ? p->screen.rows - 1 : 1); break;
                case SB_PAGEDOWN: pos += (p->screen.rows > 1 ? p->screen.rows - 1 : 1); break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: { SCROLLINFO si; memset(&si,0,sizeof(si)); si.cbSize=sizeof(si); si.fMask=SIF_TRACKPOS; if (GetScrollInfo(hwnd, SB_VERT, &si)) pos = si.nTrackPos; break; }
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = max_offset; break;
                default: break;
            }
            if (pos < 0) pos = 0; if (pos > max_offset) pos = max_offset;
            screen_set_viewport_offset(&p->screen, max_offset - pos);
            LeaveCriticalSection(&g_lock);
            update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_USER: update_native_scrollbar(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;

        case WM_CLOSE:
            if (g_cfg.general.confirm_exit) {
                if (MessageBoxW(hwnd, L"Close Wsh?", L"Wsh", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            }
            DestroyWindow(hwnd); return 0;

        case WM_DESTROY:
            for (int i = 0; i < g_pane_count; ++i) pane_stop(&g_panes[i]);
            PostQuitMessage(0); return 0;

        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static int wsh_run(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    wsh_unicode_init_process();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WSH_LOG_INFO("Wsh v" WSH_VERSION " starting");

    config_defaults(&g_cfg);
    char cfg_path[MAX_PATH]; config_path(cfg_path, MAX_PATH);
    if (!path_exists(cfg_path)) config_save_defaults(cfg_path);
    config_load(&g_cfg, cfg_path);
    apply_configured_theme(&g_cfg);

    if (g_cfg.general.default_cwd[0] && strcmp(g_cfg.general.default_cwd, "~") != 0) {
        wchar_t *wcwd = u8_to_u16(g_cfg.general.default_cwd, NULL);
        if (wcwd) { if (!SetCurrentDirectoryW(wcwd)) WSH_LOG_WARN("Failed to set default_cwd: %s", g_cfg.general.default_cwd); str_free(wcwd); }
    }

    InitializeCriticalSection(&g_lock);
    ensure_user_zshrc();

    if (!window_register_class(hInst)) return 1;
    g_hwnd = window_create(hInst, &g_cfg, nShow);
    if (!g_hwnd) return 1;
    apply_window_title(g_hwnd, &g_cfg);

    if (!renderer_init(&g_renderer, g_hwnd, &g_cfg)) return 1;

    RECT rc; GetClientRect(g_hwnd, &rc);
    g_panes[0].rect = rc;
    g_pane_count = 1;
    g_active_pane = 0;
    pane_start(&g_panes[0]);
    layout_panes(g_hwnd);
    update_native_scrollbar(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    renderer_destroy(&g_renderer);
    DeleteCriticalSection(&g_lock);
    WSH_LOG_INFO("Wsh exited with code %d", (int)msg.wParam);
    wsh_log_close();
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    wsh_log_init_default("Wsh");
    wsh_log_set_max_file_size(10ULL * 1024ULL * 1024ULL);
    wsh_log_install_crash_handlers();
    try { return wsh_run(hInst, hPrev, lpCmd, nShow); }
    catch (const std::exception& ex) { WSH_LOG_ERROR("Unhandled C++ exception: %s", ex.what()); }
    catch (...) { WSH_LOG_ERROR("Unhandled unknown exception"); }
    wsh_log_close();
    MessageBoxW(NULL, L"Wsh crashed. Details were written to %LOCALAPPDATA%\\Wsh\\logs\\wsh.log", L"Wsh error", MB_OK | MB_ICONERROR);
    return 1;
}
