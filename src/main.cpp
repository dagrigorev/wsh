/*
 * main.c — Wsh entry point.
 *
 * Responsibilities (Single Responsibility):
 *   1. DPI awareness
 *   2. Config loading
 *   3. Window creation
 *   4. Wiring: renderer ↔ screen ↔ VT parser ↔ shell ↔ REPL ↔ PTY
 *   5. Win32 message pump
 *
 * All subsystem logic lives in its own module.  main.c only calls APIs;
 * it never implements logic.
 *
 * SOLID / Dependency Inversion:
 *   The shell receives an IShellIO* (TerminalIO, defined below) that writes
 *   bytes through the VT parser into the screen buffer.  Tests substitute
 *   a BufIO that captures to a string.  main.c owns the concrete impl.
 *
 * Observer pattern:
 *   PTY reader thread fires on_pty_data() → vt_parser_feed() → InvalidateRect.
 *   Shell writes fire through the same callback path.
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

/* ══════════════════════════════════════════════════════════════════════════
   CONCRETE IShellIO IMPLEMENTATION (Terminal → VT parser → Screen)
   ══════════════════════════════════════════════════════════════════════════ */

/*
 * TerminalIO wraps IShellIO and routes write() calls through the VT parser
 * so all ANSI escape codes produced by built-ins are rendered correctly.
 *
 * This is the Dependency Inversion Principle in practice: ShellContext
 * depends only on IShellIO; the concrete rendering pipeline is here.
 */
typedef struct {
    IShellIO  base;           /* MUST be first — enables safe up-cast */
    HWND      hwnd;           /* for InvalidateRect */
    VtParser *vt;             /* VT/ANSI parser */
    CRITICAL_SECTION *lock;   /* screen buffer lock */
} TerminalIO;

static void terminal_write(IShellIO *self, const char *buf, int len) {
    TerminalIO *t = (TerminalIO *)self;
    EnterCriticalSection(t->lock);
    vt_parser_feed(t->vt, buf, len);
    LeaveCriticalSection(t->lock);
    InvalidateRect(t->hwnd, NULL, FALSE);
}

static int terminal_read_line(IShellIO *self, char *buf, int size) {
    /* Built-in shell REPL handles keyboard input via repl_handle_input();
     * this path is used only by the 'read' built-in. */
    (void)self;
    if (!buf || size <= 0) return 0;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n = 0;
    ReadFile(hin, buf, (DWORD)(size - 1), &n, NULL);
    buf[n] = '\0';
    return (int)n;
}

/* ══════════════════════════════════════════════════════════════════════════
   GLOBAL APPLICATION STATE
   ══════════════════════════════════════════════════════════════════════════ */

static HWND              g_hwnd     = NULL;
static Config            g_cfg      = {0};
static ScreenBuffer      g_screen   = {0};
static VtParser          g_vt       = {0};
static Renderer          g_renderer = {0};
static ShellContext      g_shell    = {0};
static PtySession        g_pty      = {0};
static Repl              g_repl     = {0};
static TerminalIO        g_io       = {0};
static CRITICAL_SECTION  g_lock;
static bool              g_use_pty  = false;
static bool              g_suppress_char = false;

static void get_exe_dir(char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    GetModuleFileNameA(NULL, out, (DWORD)out_size);
    char *last_bs = strrchr(out, '\\');
    if (last_bs) *last_bs = '\0';
}

static void expand_title_template(const Config *cfg, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    const char *tmpl = (cfg && cfg->general.title[0]) ? cfg->general.title : "Wsh - ${cwd}";
    char cwd[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, cwd);

    int oi = 0;
    for (const char *p = tmpl; *p && oi < out_size - 1; ) {
        if (strncmp(p, "${cwd}", 6) == 0) {
            for (const char *c = cwd; *c && oi < out_size - 1; c++) out[oi++] = *c;
            p += 6;
        } else if (strncmp(p, "${theme}", 8) == 0) {
            const char *theme = (cfg && cfg->general.theme[0]) ? cfg->general.theme : "default";
            for (const char *c = theme; *c && oi < out_size - 1; c++) out[oi++] = *c;
            p += 8;
        } else if (strncmp(p, "${version}", 10) == 0) {
            const char *ver = WSH_VERSION;
            for (const char *c = ver; *c && oi < out_size - 1; c++) out[oi++] = *c;
            p += 10;
        } else {
            out[oi++] = *p++;
        }
    }
    out[oi] = '\0';
}

static void apply_configured_theme(Config *cfg) {
    if (!cfg || !cfg->general.theme[0]) return;
    if (strchr(cfg->general.theme, '\\') || strchr(cfg->general.theme, '/') || strchr(cfg->general.theme, ':')) {
        if (!config_apply_theme_file(cfg, cfg->general.theme))
            WSH_LOG_WARN("Theme file not found: %s", cfg->general.theme);
        return;
    }

    char exe_dir[MAX_PATH]; get_exe_dir(exe_dir, MAX_PATH);
    char theme_path[MAX_PATH];
    _snprintf(theme_path, MAX_PATH, "%s\\themes\\%s.toml", exe_dir, cfg->general.theme);
    if (config_apply_theme_file(cfg, theme_path)) return;

    char appdata[MAX_PATH] = {0};
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    _snprintf(theme_path, MAX_PATH, "%s\\Wsh\\themes\\%s.toml", appdata, cfg->general.theme);
    if (!config_apply_theme_file(cfg, theme_path))
        WSH_LOG_WARN("Configured theme was not found: %s", cfg->general.theme);
}

static void apply_window_title(HWND hwnd, const Config *cfg) {
    char title[512];
    expand_title_template(cfg, title, (int)sizeof(title));
    wchar_t *wtitle = u8_to_u16(title, NULL);
    if (wtitle) { SetWindowTextW(hwnd, wtitle); str_free(wtitle); }
}

/* ══════════════════════════════════════════════════════════════════════════
   PTY READER CALLBACK
   ══════════════════════════════════════════════════════════════════════════ */

static void on_pty_data(const char *buf, int len, void *ud) {
    (void)ud;
    /* Called from reader thread — must hold the lock */
    EnterCriticalSection(&g_lock);
    vt_parser_feed(&g_vt, buf, len);
    LeaveCriticalSection(&g_lock);
    InvalidateRect(g_hwnd, NULL, FALSE);
    PostMessage(g_hwnd, WM_USER, 0, 0);
}

/* ══════════════════════════════════════════════════════════════════════════
   TITLE CHANGE CALLBACK (from VT parser OSC 0/2)
   ══════════════════════════════════════════════════════════════════════════ */

static void on_title(const char *title, void *ud) {
    (void)ud;
    wchar_t *w = u8_to_u16(title, NULL);
    if (w) { SetWindowTextW(g_hwnd, w); str_free(w); }
}

/* ══════════════════════════════════════════════════════════════════════════
   WndProc
   ══════════════════════════════════════════════════════════════════════════ */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

        /* ── Paint ──────────────────────────────────────────────────────── */
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            EnterCriticalSection(&g_lock);
            renderer_paint(&g_renderer, &g_screen, true,
                           g_screen.cursor_x, g_screen.cursor_y);
            LeaveCriticalSection(&g_lock);
            EndPaint(hwnd, &ps);
            return 0;
        }

        /* ── Resize ─────────────────────────────────────────────────────── */
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (w > 0 && h > 0) {
                renderer_resize(&g_renderer, w, h);
                int cols = g_renderer.cols, rows = g_renderer.rows;
                EnterCriticalSection(&g_lock);
                screen_resize(&g_screen, cols, rows);
                LeaveCriticalSection(&g_lock);
                if (g_use_pty) pty_resize(&g_pty, cols, rows);
            }
            return 0;
        }

        /* ── DPI change ─────────────────────────────────────────────────── */
        case WM_DPICHANGED: {
            renderer_update_dpi(&g_renderer, (float)HIWORD(wParam));
            const RECT *rc = (const RECT *)lParam;
            SetWindowPos(hwnd, NULL, rc->left, rc->top,
                         rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* ── Cursor blink timer ─────────────────────────────────────────── */
        case WM_TIMER:
            if (wParam == 1) {
                renderer_toggle_cursor_blink(&g_renderer);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        /* ── Focus ──────────────────────────────────────────────────────── */
        case WM_SETFOCUS:
            g_screen.cursor_visible = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_KILLFOCUS:
            g_screen.cursor_visible = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        /* ── Keyboard ───────────────────────────────────────────────────── */
        case WM_KEYDOWN: {
            InputEvent ev = input_translate(wParam, 0, lParam,
                                             g_screen.app_cursor_keys);
            g_suppress_char = input_suppress_char(wParam, lParam);

            switch (ev.action) {
                case INPUT_COPY:
                    if (OpenClipboard(hwnd)) {
                        /* Copy selected region — simplified: copy whole line */
                        CloseClipboard();
                    }
                    break;

                case INPUT_PASTE:
                    if (OpenClipboard(hwnd)) {
                        HANDLE hd = GetClipboardData(CF_TEXT);
                        if (hd) {
                            const char *text = (const char *)GlobalLock(hd);
                            if (text) {
                                if (g_use_pty) pty_write(&g_pty, text, (int)strlen(text));
                                else repl_handle_input(&g_repl, text, (int)strlen(text));
                                GlobalUnlock(hd);
                            }
                        }
                        CloseClipboard();
                    }
                    break;

                case INPUT_SCROLL_UP:
                    EnterCriticalSection(&g_lock);
                    screen_scroll_viewport(&g_screen, 3);
                    LeaveCriticalSection(&g_lock);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;

                case INPUT_SCROLL_DOWN:
                    EnterCriticalSection(&g_lock);
                    screen_scroll_viewport(&g_screen, -3);
                    LeaveCriticalSection(&g_lock);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;

                case INPUT_ZOOM_IN:
                    renderer_set_font(&g_renderer, g_cfg.font.family,
                                      g_renderer.font.pt_size + 1.0f);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;

                case INPUT_ZOOM_OUT:
                    if (g_renderer.font.pt_size > 6.0f)
                        renderer_set_font(&g_renderer, g_cfg.font.family,
                                          g_renderer.font.pt_size - 1.0f);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;

                case INPUT_CHAR:
                    if (ev.len > 0) {
                        /* Reset viewport to live buffer on any keypress */
                        if (g_screen.viewport_offset) {
                            EnterCriticalSection(&g_lock);
                            g_screen.viewport_offset = 0;
                            screen_mark_dirty_all(&g_screen);
                            LeaveCriticalSection(&g_lock);
                        }
                        if (g_use_pty) pty_write(&g_pty, ev.bytes, ev.len);
                        else {
                            if (!repl_handle_input(&g_repl, ev.bytes, ev.len))
                                PostQuitMessage(0);
                        }
                    }
                    break;

                default: break;
            }
            return 0;
        }

        case WM_CHAR: {
            if (g_suppress_char) { g_suppress_char = false; return 0; }
            WCHAR ch = (WCHAR)wParam;
            if (ch == '\r') return 0;
            InputEvent ev = input_translate(0, ch, lParam, g_screen.app_cursor_keys);
            if (ev.action == INPUT_CHAR && ev.len > 0) {
                if (g_screen.viewport_offset) {
                    EnterCriticalSection(&g_lock);
                    g_screen.viewport_offset = 0;
                    screen_mark_dirty_all(&g_screen);
                    LeaveCriticalSection(&g_lock);
                }
                if (g_use_pty) pty_write(&g_pty, ev.bytes, ev.len);
                else if (!repl_handle_input(&g_repl, ev.bytes, ev.len))
                    PostQuitMessage(0);
            }
            return 0;
        }

        /* ── Mouse selection ───────────────────────────────────────────── */
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            int col, row;
            renderer_pixel_to_cell(&g_renderer, mx, my, &col, &row);
            g_renderer.sel_start_col = g_renderer.sel_end_col = col;
            g_renderer.sel_start_row = g_renderer.sel_end_row = row;
            g_renderer.sel_active = true;
            g_renderer.sel_valid  = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_renderer.sel_active) {
                int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
                int col, row;
                renderer_pixel_to_cell(&g_renderer, mx, my, &col, &row);
                g_renderer.sel_end_col = col;
                g_renderer.sel_end_row = row;
                g_renderer.sel_valid = (col != g_renderer.sel_start_col ||
                                        row != g_renderer.sel_start_row);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            g_renderer.sel_active = false;
            if (g_renderer.sel_valid && OpenClipboard(hwnd)) {
                int sr = g_renderer.sel_start_row, sc = g_renderer.sel_start_col;
                int er = g_renderer.sel_end_row,   ec = g_renderer.sel_end_col;
                if (sr > er || (sr == er && sc > ec)) {
                    int tr=sr,tc=sc; sr=er;sc=ec; er=tr;ec=tc;
                }
                char text[65536]; int ti = 0;
                EnterCriticalSection(&g_lock);
                for (int r = sr; r <= er && ti < 65530; r++) {
                    int c0 = (r == sr) ? sc : 0;
                    int c1 = (r == er) ? ec : g_screen.cols - 1;
                    int last_ns = c0 - 1;
                    for (int c = c0; c <= c1; c++) {
                        if (r < g_screen.rows && c < g_screen.cols &&
                            g_screen.cells[r * g_screen.cols + c].ch > ' ')
                            last_ns = c;
                    }
                    for (int c = c0; c <= last_ns && ti < 65528; c++) {
                        uint32_t ch = (r < g_screen.rows && c < g_screen.cols)
                            ? g_screen.cells[r * g_screen.cols + c].ch : ' ';
                        if (!ch) ch = ' ';
                        if (ch < 0x80) { text[ti++] = (char)ch; }
                        else if (ch < 0x800) { text[ti++]=(char)(0xC0|(ch>>6)); text[ti++]=(char)(0x80|(ch&0x3F)); }
                        else { text[ti++]=(char)(0xE0|(ch>>12)); text[ti++]=(char)(0x80|((ch>>6)&0x3F)); text[ti++]=(char)(0x80|(ch&0x3F)); }
                    }
                    if (r < er) { text[ti++] = '\r'; text[ti++] = '\n'; }
                }
                text[ti] = '\0';
                LeaveCriticalSection(&g_lock);
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (size_t)(ti + 1));
                if (hg) {
                    memcpy(GlobalLock(hg), text, (size_t)(ti + 1));
                    GlobalUnlock(hg);
                    EmptyClipboard();
                    SetClipboardData(CF_TEXT, hg);
                }
                CloseClipboard();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            /* Right-click pastes from clipboard */
            if (OpenClipboard(hwnd)) {
                HANDLE hd = GetClipboardData(CF_TEXT);
                if (hd) {
                    const char *txt = (const char *)GlobalLock(hd);
                    if (txt) {
                        if (g_use_pty) pty_write(&g_pty, txt, (int)strlen(txt));
                        else repl_handle_input(&g_repl, txt, (int)strlen(txt));
                        GlobalUnlock(hd);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }

        /* ── Mouse wheel ────────────────────────────────────────────────── */
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            EnterCriticalSection(&g_lock);
            screen_scroll_viewport(&g_screen, -delta * 3);
            LeaveCriticalSection(&g_lock);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* ── PTY/shell data arrived ─────────────────────────────────────── */
        case WM_USER:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        /* ── Close ──────────────────────────────────────────────────────── */
        case WM_CLOSE:
            if (g_cfg.general.confirm_exit) {
                if (MessageBoxW(hwnd, L"Close Wsh?", L"Wsh",
                                MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_use_pty) pty_close(&g_pty);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   WinMain
   ══════════════════════════════════════════════════════════════════════════ */

static int wsh_run(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;

    /* ── 1. DPI awareness ─────────────────────────────────────────────────── */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    /* ── 2. Logging (debug builds write to OutputDebugString) ─────────────── */
    WSH_LOG_INFO("Wsh v" WSH_VERSION " starting");

    /* ── 3. Config ────────────────────────────────────────────────────────── */
    config_defaults(&g_cfg);
    char cfg_path[MAX_PATH];
    config_path(cfg_path, MAX_PATH);
    if (!path_exists(cfg_path)) config_save_defaults(cfg_path);
    config_load(&g_cfg, cfg_path);
    apply_configured_theme(&g_cfg);

    if (g_cfg.general.default_cwd[0] && strcmp(g_cfg.general.default_cwd, "~") != 0) {
        wchar_t *wcwd = u8_to_u16(g_cfg.general.default_cwd, NULL);
        if (wcwd) {
            if (!SetCurrentDirectoryW(wcwd)) {
                WSH_LOG_WARN("Failed to set default_cwd: %s", g_cfg.general.default_cwd);
            }
            str_free(wcwd);
        }
    }

    /* ── 4. Screen buffer + CRITICAL_SECTION ──────────────────────────────── */
    InitializeCriticalSection(&g_lock);
    screen_init(&g_screen, 80, 24, g_cfg.general.scrollback);

    /* ── 5. Window ────────────────────────────────────────────────────────── */
    if (!window_register_class(hInst)) return 1;
    g_hwnd = window_create(hInst, &g_cfg, nShow);
    if (!g_hwnd) return 1;
    apply_window_title(g_hwnd, &g_cfg);

    /* ── 6. Renderer ──────────────────────────────────────────────────────── */
    if (!renderer_init(&g_renderer, g_hwnd, &g_cfg)) return 1;

    /* ── 7. VT parser ─────────────────────────────────────────────────────── */
    vt_parser_init(&g_vt, &g_screen);
    g_vt.on_title = on_title;
    g_vt.userdata = NULL;

    /* ── 8. Shell + IShellIO wiring ───────────────────────────────────────── */
    g_io.base.write     = terminal_write;
    g_io.base.read_line = terminal_read_line;
    g_io.hwnd           = g_hwnd;
    g_io.vt             = &g_vt;
    g_io.lock           = &g_lock;

    shell_ctx_init(&g_shell, (IShellIO *)&g_io);

    /* ── 9. REPL ──────────────────────────────────────────────────────────── */
    repl_init(&g_repl, &g_shell);

    /* ── 10. Source ~/.zshrc if present ───────────────────────────────────── */
    char zshrc[MAX_PATH] = {0};
    char profile[MAX_PATH] = {0};
    GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
    _snprintf(zshrc, MAX_PATH, "%s\\.zshrc", profile);

    /* Seed ~/.zshrc only once; never overwrite a user's existing config. */
    {
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

    if (path_exists(zshrc)) {
        WSH_LOG_INFO("Sourcing %s", zshrc);
        shell_source(&g_shell, zshrc);
    }

    /* ── 11. Decide: built-in shell or PTY passthrough ────────────────────── */
    if (strcmp(g_cfg.general.shell, "wsh") == 0 || g_cfg.general.shell[0] == '\0') {
        g_use_pty = false;
        repl_show_prompt(&g_repl);
    } else {
        g_use_pty = true;
        if (!pty_create(&g_pty, g_renderer.cols, g_renderer.rows)) {
            WSH_LOG_WARN("pty_create failed — using built-in shell");
            g_use_pty = false;
        } else {
            wchar_t *wcmd = u8_to_u16(g_cfg.general.shell, NULL);
            if (!pty_spawn(&g_pty, wcmd ? wcmd : L"cmd.exe", NULL,
                           on_pty_data, NULL)) {
                WSH_LOG_ERROR("pty_spawn failed");
                pty_close(&g_pty); g_use_pty = false;
            }
            str_free(wcmd);
        }
        if (!g_use_pty) repl_show_prompt(&g_repl);
    }

    /* ── 12. Sync grid size ───────────────────────────────────────────────── */
    {
        RECT rc; GetClientRect(g_hwnd, &rc);
        renderer_resize(&g_renderer, rc.right - rc.left, rc.bottom - rc.top);
        EnterCriticalSection(&g_lock);
        screen_resize(&g_screen, g_renderer.cols, g_renderer.rows);
        LeaveCriticalSection(&g_lock);
        if (g_use_pty) pty_resize(&g_pty, g_renderer.cols, g_renderer.rows);
    }

    /* ── 13. Message pump ─────────────────────────────────────────────────── */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* ── 14. Teardown ─────────────────────────────────────────────────────── */
    repl_free(&g_repl);
    shell_ctx_free(&g_shell);
    renderer_destroy(&g_renderer);
    screen_free(&g_screen);
    DeleteCriticalSection(&g_lock);

    WSH_LOG_INFO("Wsh exited with code %d", (int)msg.wParam);
    wsh_log_close();
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    wsh_log_init_default("Wsh");
    wsh_log_set_max_file_size(10ULL * 1024ULL * 1024ULL);
    wsh_log_install_crash_handlers();

    try {
        return wsh_run(hInst, hPrev, lpCmd, nShow);
    } catch (const std::exception& ex) {
        WSH_LOG_ERROR("Unhandled C++ exception: %s", ex.what());
    } catch (...) {
        WSH_LOG_ERROR("Unhandled unknown C++ exception");
    }

    wsh_log_close();
    MessageBoxW(NULL, L"Wsh crashed. Details were written to %LOCALAPPDATA%\\Wsh\\logs\\wsh.log",
                L"Wsh error", MB_OK | MB_ICONERROR);
    return 1;
}
