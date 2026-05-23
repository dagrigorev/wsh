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
#include <shlwapi.h>

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
#include "ai/wsh_ai.h"

#define WSH_MAX_PANES 4
#define WSH_MAX_TABS 8
#define WSH_UI_TITLE_H 42
#define WSH_UI_TOOLBAR_H 40
#define WSH_UI_TERM_HEADER_H 30
#define WSH_UI_STATUS_H 24
#define WSH_UI_SIDEBAR_W 196
#define WSH_UI_TAB_W 188
#define WSH_UI_MAX_HITS 256

typedef struct {
    uint32_t bg_base;
    uint32_t bg_surface;
    uint32_t bg_card;
    uint32_t bg_hover;
    uint32_t bg_active;
    uint32_t border;
    uint32_t border_mid;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_faint;
    uint32_t accent;
    uint32_t green;
    uint32_t red;
    uint32_t amber;
    uint32_t purple;
    uint32_t cyan;
} ThemeModel;

typedef struct {
    double cpu_percent;
    DWORD memory_load;
    ULONGLONG disk_free;
    ULONGLONG disk_total;
    bool cpu_available;
    bool memory_available;
    bool disk_available;
    FILETIME last_idle;
    FILETIME last_kernel;
    FILETIME last_user;
    bool has_cpu_sample;
} ResourceUsageModel;

typedef struct {
    char branch[128];
    char project[128];
    char repo_root[MAX_PATH];
    DWORD last_tick;
    bool inside_repo;
} GitStatusModel;

typedef struct {
    RECT rect;
    int tab_index;
} TabHitRect;

typedef struct {
    RECT add_tab;
    RECT split;
    RECT search;
    RECT settings;
    RECT sidebar_toggle;
    RECT kill;
    RECT duplicate;
    RECT tab_close[WSH_MAX_TABS];
    TabHitRect tabs[WSH_MAX_TABS];
    int tab_count;
} UiHitTargets;

typedef struct {
    bool open;
    char query[128];
    int match_count;
    int active_match;
} SearchState;

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
    char         startup_cwd[MAX_PATH];
    char         display_cwd[MAX_PATH];
    DWORD        last_exit_code;
    bool         has_exit_code;
};

struct TerminalTab {
    TerminalPane panes[WSH_MAX_PANES];
    int          pane_count;
    int          active_pane;
    bool         initialized;
};

static HWND              g_hwnd     = NULL;
static Config            g_cfg      = {0};
static Renderer          g_renderer = {0};
static TerminalTab       g_tabs[WSH_MAX_TABS];
static int               g_tab_count = 0;
static int               g_active_tab = 0;
static CRITICAL_SECTION  g_lock;
static bool              g_suppress_char = false;
static bool              g_sidebar_visible = true;
static ThemeModel        g_theme = {0};
static ResourceUsageModel g_resources = {0};
static GitStatusModel    g_git = {0};
static UiHitTargets      g_hits = {0};
static SearchState       g_search = {0};

static void update_native_scrollbar(HWND hwnd);
static void layout_panes(HWND hwnd);
static void refresh_runtime_status(bool force);
static const char *pane_cwd(const TerminalPane *p);

static const char *basename_const(const char *path) {
    if (!path || !path[0]) return "";
    const char *last1 = strrchr(path, '\\');
    const char *last2 = strrchr(path, '/');
    const char *last = last1 > last2 ? last1 : last2;
    if (!last) return path;
    return last[1] ? last + 1 : path;
}

static TerminalTab *active_tab(void) {
    if (g_active_tab < 0) g_active_tab = 0;
    if (g_active_tab >= g_tab_count) g_active_tab = g_tab_count - 1;
    if (g_tab_count <= 0) return NULL;
    if (g_active_tab >= 0 && g_tabs[g_active_tab].initialized) return &g_tabs[g_active_tab];
    for (int i = 0; i < g_tab_count; ++i) {
        if (g_tabs[i].initialized) {
            g_active_tab = i;
            return &g_tabs[i];
        }
    }
    return NULL;
}

static TerminalPane *active_pane(void) {
    TerminalTab *tab = active_tab();
    if (!tab) return NULL;
    if (tab->active_pane < 0) tab->active_pane = 0;
    if (tab->active_pane >= tab->pane_count) tab->active_pane = tab->pane_count - 1;
    return tab->pane_count > 0 ? &tab->panes[tab->active_pane] : NULL;
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

static void source_user_zshrc(ShellContext *shell) {
    (void)shell;
    WSH_LOG_DEBUG("Skipping synchronous ~/.zshrc source during GUI pane startup");
}

static void apply_default_gui_prompt(ShellContext *shell) {
    if (!shell) return;
    if (shell_getenv(shell, "PROMPT") || shell_getenv(shell, "PS1")) return;
    shell_setenv(shell, "PROMPT",
                 "\x1b[1;32m%n\x1b[0m\x1b[2m@\x1b[0m"
                 "\x1b[1;34m%m\x1b[0m "
                 "\x1b[1;36m%C\x1b[0m "
                 "\x1b[1;35m>\x1b[0m ",
                 false);
}

#define WM_WSH_EXEC_DONE (WM_USER + 100)

static void repl_exec_done_cb(Repl *r, int exec_result) {
    (void)exec_result;
    /* Find the pane that owns this REPL and post a message to the main window.
     * We walk the tabs/panes to find the matching Repl pointer. */
    for (int ti = 0; ti < g_tab_count; ++ti) {
        TerminalTab *tab = &g_tabs[ti];
        if (!tab->initialized) continue;
        for (int pi = 0; pi < tab->pane_count; ++pi) {
            TerminalPane *pane = &tab->panes[pi];
            if (&pane->repl == r) {
                PostMessageW(g_hwnd, WM_WSH_EXEC_DONE, (WPARAM)ti, (LPARAM)pi);
                return;
            }
        }
    }
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
    WSH_LOG_DEBUG("pane_start begin");

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

    char desired_cwd[MAX_PATH] = {0};
    if (pane->startup_cwd[0]) strncpy(desired_cwd, pane->startup_cwd, MAX_PATH - 1);

    memset(pane, 0, sizeof(*pane));
    pane->rect = initial_rect;
    if (desired_cwd[0]) strncpy(pane->startup_cwd, desired_cwd, MAX_PATH - 1);
    if (!pane->startup_cwd[0]) GetCurrentDirectoryA(MAX_PATH, pane->startup_cwd);
    strncpy(pane->display_cwd, pane->startup_cwd, MAX_PATH - 1);

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

    wchar_t *old_cwd = NULL;
    if (pane->startup_cwd[0]) {
        wchar_t wcwd[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, wcwd)) {
            old_cwd = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(wchar_t) * MAX_PATH);
            if (old_cwd) { wcsncpy(old_cwd, wcwd, MAX_PATH - 1); old_cwd[MAX_PATH - 1] = L'\0'; }
        }
        wchar_t *target = u8_to_u16(pane->startup_cwd, NULL);
        if (target) { SetCurrentDirectoryW(target); str_free(target); }
    }
    shell_ctx_init(&pane->shell, (IShellIO *)&pane->io);
    if (old_cwd) {
        SetCurrentDirectoryW(old_cwd);
        HeapFree(GetProcessHeap(), 0, old_cwd);
    }
    /* Initialize proactive AI reasoning micro-model */
    if (pane->shell.ai_enabled) {
        wsh_ai_init_reasoning(&pane->shell);
    }
    source_user_zshrc(&pane->shell);
    apply_default_gui_prompt(&pane->shell);
    repl_init(&pane->repl, &pane->shell);
    repl_set_on_exec_done(&pane->repl, repl_exec_done_cb);

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
            wchar_t *wcwd = u8_to_u16(pane->startup_cwd, NULL);
            if (!pty_spawn(&pane->pty, wcmd ? wcmd : L"cmd.exe", wcwd, on_pty_data, pane)) {
                WSH_LOG_ERROR("pane pty_spawn failed");
                pty_close(&pane->pty);
                pane->use_pty = false;
            }
            str_free(wcwd);
            str_free(wcmd);
        }
        if (!pane->use_pty) repl_show_prompt(&pane->repl);
    }
    WSH_LOG_DEBUG("pane_start complete use_pty=%d", pane->use_pty ? 1 : 0);
    return true;
}

static void pane_stop(TerminalPane *pane) {
    if (!pane || !pane->initialized) return;
    if (pane->use_pty) {
        pane->last_exit_code = pty_exit_code(&pane->pty);
        pane->has_exit_code = true;
        pty_close(&pane->pty);
    }
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

static void terminal_view_rect(HWND hwnd, RECT *out) {
    if (!out) return;
    GetClientRect(hwnd, out);
    out->left += g_sidebar_visible ? WSH_UI_SIDEBAR_W : 0;
    out->top += WSH_UI_TITLE_H + WSH_UI_TOOLBAR_H + WSH_UI_TERM_HEADER_H;
    out->bottom -= WSH_UI_STATUS_H;
    if (out->right <= out->left) out->right = out->left + 1;
    if (out->bottom <= out->top) out->bottom = out->top + 1;
}

static void layout_panes(HWND hwnd) {
    TerminalTab *tab = active_tab();
    if (!hwnd || !tab || tab->pane_count <= 0) return;
    g_renderer.reserved_left_px = g_sidebar_visible ? WSH_UI_SIDEBAR_W : 0;
    g_renderer.reserved_top_px = WSH_UI_TITLE_H + WSH_UI_TOOLBAR_H + WSH_UI_TERM_HEADER_H;
    g_renderer.reserved_bottom_px = WSH_UI_STATUS_H;
    RECT rc; terminal_view_rect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    RECT r[WSH_MAX_PANES] = {0};
    if (tab->pane_count == 1) {
        r[0] = rc;
    } else if (tab->pane_count == 2) {
        int mid = rc.left + w / 2;
        r[0] = {rc.left, rc.top, mid, rc.bottom};
        r[1] = {mid, rc.top, rc.right, rc.bottom};
    } else if (tab->pane_count == 3) {
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
    for (int i = 0; i < tab->pane_count; ++i) {
        tab->panes[i].rect = r[i];
        pane_resize(&tab->panes[i]);
    }
    LeaveCriticalSection(&g_lock);
    update_native_scrollbar(hwnd);
}

static int pane_hit_test(int x, int y) {
    TerminalTab *tab = active_tab();
    if (!tab) return 0;
    POINT pt = {x, y};
    for (int i = 0; i < tab->pane_count; ++i) {
        if (PtInRect(&tab->panes[i].rect, pt)) return i;
    }
    return tab->active_pane;
}

static bool pane_add(void) {
    TerminalTab *tab = active_tab();
    if (!tab || tab->pane_count >= WSH_MAX_PANES) return false;
    TerminalPane *src = active_pane();
    int idx = tab->pane_count++;
    memset(&tab->panes[idx], 0, sizeof(tab->panes[idx]));
    tab->panes[idx].rect = src ? src->rect : RECT{0, 0, 800, 600};
    if (src && pane_cwd(src)[0]) strncpy(tab->panes[idx].startup_cwd, pane_cwd(src), MAX_PATH - 1);
    if (!pane_start(&tab->panes[idx])) {
        memset(&tab->panes[idx], 0, sizeof(tab->panes[idx]));
        tab->pane_count--;
        return false;
    }
    tab->active_pane = idx;
    layout_panes(g_hwnd);
    refresh_runtime_status(true);
    return true;
}

static void pane_close_active(void) {
    TerminalTab *tab = active_tab();
    if (!tab || tab->pane_count <= 1) return;

    /* Keep this operation pointer-safe for ConPTY callbacks.  Reader callbacks
       store the address of their TerminalPane as userdata, so panes must not be
       memmoved while a child process is alive.  For this first pane pass we close
       the last physical pane; when the focused pane is not last, focus is moved
       to the last pane before closing. */
    int idx = tab->active_pane;
    if (idx != tab->pane_count - 1) {
        tab->active_pane = tab->pane_count - 1;
        idx = tab->active_pane;
    }

    pane_stop(&tab->panes[idx]);
    memset(&tab->panes[idx], 0, sizeof(tab->panes[idx]));
    tab->pane_count--;
    if (tab->active_pane >= tab->pane_count) tab->active_pane = tab->pane_count - 1;
    layout_panes(g_hwnd);
    refresh_runtime_status(true);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static bool tab_start(TerminalTab *tab, const char *cwd) {
    if (!tab) return false;
    memset(tab, 0, sizeof(*tab));
    tab->pane_count = 1;
    tab->active_pane = 0;
    if (cwd && cwd[0]) strncpy(tab->panes[0].startup_cwd, cwd, MAX_PATH - 1);
    tab->initialized = pane_start(&tab->panes[0]);
    return tab->initialized;
}

static bool tab_add(void) {
    int idx = -1;
    for (int i = 0; i < WSH_MAX_TABS; ++i) {
        if (!g_tabs[i].initialized) { idx = i; break; }
    }
    if (idx < 0) return false;
    TerminalPane *src = active_pane();
    char cwd[MAX_PATH] = {0};
    if (src && pane_cwd(src)[0]) strncpy(cwd, pane_cwd(src), MAX_PATH - 1);
    if (!cwd[0]) GetCurrentDirectoryA(MAX_PATH, cwd);
    if (!tab_start(&g_tabs[idx], cwd)) {
        memset(&g_tabs[idx], 0, sizeof(g_tabs[idx]));
        return false;
    }
    if (idx >= g_tab_count) g_tab_count = idx + 1;
    g_active_tab = idx;
    layout_panes(g_hwnd);
    refresh_runtime_status(true);
    InvalidateRect(g_hwnd, NULL, FALSE);
    return true;
}

static void tab_stop(TerminalTab *tab) {
    if (!tab || !tab->initialized) return;
    for (int i = 0; i < tab->pane_count; ++i) pane_stop(&tab->panes[i]);
    memset(tab, 0, sizeof(*tab));
}

static bool pane_has_running_process(const TerminalPane *pane) {
    if (!pane || !pane->initialized) return false;
    if (pane->use_pty) return pty_is_alive((PtySession *)&pane->pty);
    return false;
}

static bool tab_has_running_process(const TerminalTab *tab) {
    if (!tab) return false;
    for (int i = 0; i < tab->pane_count; ++i) {
        if (pane_has_running_process(&tab->panes[i])) return true;
    }
    return false;
}

static void tab_close_index(int idx) {
    if (idx < 0 || idx >= g_tab_count) return;
    TerminalTab *tab = &g_tabs[idx];
    if (!tab->initialized) return;
    if (tab_has_running_process(tab)) {
        if (MessageBoxW(g_hwnd, L"Close this tab and terminate its running process?", L"Wsh", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    tab_stop(tab);
    while (g_tab_count > 0 && !g_tabs[g_tab_count - 1].initialized) g_tab_count--;
    bool any = false;
    for (int i = 0; i < g_tab_count; ++i) {
        if (g_tabs[i].initialized) { any = true; break; }
    }
    if (!any) {
        tab_start(&g_tabs[0], NULL);
        g_tab_count = 1;
    }
    if (g_active_tab == idx || g_active_tab >= g_tab_count || !g_tabs[g_active_tab].initialized) {
        g_active_tab = 0;
        active_tab();
    }
    layout_panes(g_hwnd);
    refresh_runtime_status(true);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void update_native_scrollbar(HWND hwnd) {
    if (hwnd) ShowScrollBar(hwnd, SB_VERT, FALSE);
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

static D2D1_COLOR_F d2d_rgb(uint32_t rgb, float alpha = 1.0f) {
    D2D1_COLOR_F c = { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha };
    return c;
}

static void theme_material_cyber_dark(ThemeModel *t) {
    t->bg_base = 0x0d0f14; t->bg_surface = 0x13161e; t->bg_card = 0x181c26; t->bg_hover = 0x1e2333;
    t->bg_active = 0x232840; t->border = 0x252a36; t->border_mid = 0x343a4a;
    t->text_primary = 0xe2e6f0; t->text_secondary = 0x7b8299; t->text_faint = 0x40465c;
    t->accent = 0x4f8ef7; t->green = 0x3dd68c; t->red = 0xf7604f; t->amber = 0xf7b84f;
    t->purple = 0xa97ff7; t->cyan = 0x4fcff7;
}

static void draw_rect(Renderer *r, const RECT *rc, uint32_t color) {
    D2D1_RECT_F d = {(float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom};
    r->bg_brush->SetColor(d2d_rgb(color));
    r->render_target->FillRectangle(d, r->bg_brush);
}

static void draw_round_rect(Renderer *r, const RECT *rc, float radius, uint32_t color) {
    D2D1_ROUNDED_RECT rr;
    rr.rect = {(float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom};
    rr.radiusX = radius;
    rr.radiusY = radius;
    r->bg_brush->SetColor(d2d_rgb(color));
    r->render_target->FillRoundedRectangle(rr, r->bg_brush);
}

static void draw_line(Renderer *r, float x1, float y1, float x2, float y2, uint32_t color) {
    r->fg_brush->SetColor(d2d_rgb(color));
    r->render_target->DrawLine({x1, y1}, {x2, y2}, r->fg_brush, 1.0f);
}

static void draw_round_border(Renderer *r, const RECT *rc, float radius, uint32_t color) {
    D2D1_ROUNDED_RECT rr;
    rr.rect = {(float)rc->left + 0.5f, (float)rc->top + 0.5f, (float)rc->right - 0.5f, (float)rc->bottom - 0.5f};
    rr.radiusX = radius;
    rr.radiusY = radius;
    r->fg_brush->SetColor(d2d_rgb(color));
    r->render_target->DrawRoundedRectangle(rr, r->fg_brush, 1.0f);
}

static void draw_text_u8(Renderer *r, const char *text, const RECT *rc, uint32_t color, IDWriteTextFormat *fmt = NULL) {
    if (!text || !text[0] || !r || !r->render_target) return;
    wchar_t *w = u8_to_u16(text, NULL);
    if (!w) return;
    D2D1_RECT_F d = {(float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom};
    r->fg_brush->SetColor(d2d_rgb(color));
    IDWriteTextFormat *use = fmt ? fmt : r->font.fmt_normal;
    r->render_target->DrawTextW(w, (UINT32)wcslen(w), use, d, r->fg_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    str_free(w);
}

static const char *shell_label(void) {
    return (strcmp(g_cfg.general.shell, "wsh") == 0 || !g_cfg.general.shell[0]) ? "wsh" : basename_const(g_cfg.general.shell);
}

static const char *pane_cwd(const TerminalPane *p) {
    if (!p) return "";
    if (!p->use_pty && p->shell.cwd[0]) return p->shell.cwd;
    if (p->display_cwd[0]) return p->display_cwd;
    if (p->startup_cwd[0]) return p->startup_cwd;
    return "";
}

static void pane_sync_cwd(TerminalPane *p) {
    if (!p) return;
    if (!p->use_pty && p->shell.cwd[0]) strncpy(p->display_cwd, p->shell.cwd, MAX_PATH - 1);
}

static bool is_process_elevated(void) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {0};
    DWORD cb = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &cb);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

static void update_resources(void) {
    FILETIME idle = {0}, kernel = {0}, user = {0};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULARGE_INTEGER li, lk, lu, pi, pk, pu;
        li.LowPart = idle.dwLowDateTime; li.HighPart = idle.dwHighDateTime;
        lk.LowPart = kernel.dwLowDateTime; lk.HighPart = kernel.dwHighDateTime;
        lu.LowPart = user.dwLowDateTime; lu.HighPart = user.dwHighDateTime;
        pi.LowPart = g_resources.last_idle.dwLowDateTime; pi.HighPart = g_resources.last_idle.dwHighDateTime;
        pk.LowPart = g_resources.last_kernel.dwLowDateTime; pk.HighPart = g_resources.last_kernel.dwHighDateTime;
        pu.LowPart = g_resources.last_user.dwLowDateTime; pu.HighPart = g_resources.last_user.dwHighDateTime;
        if (g_resources.has_cpu_sample) {
            ULONGLONG idle_delta = li.QuadPart - pi.QuadPart;
            ULONGLONG total_delta = (lk.QuadPart - pk.QuadPart) + (lu.QuadPart - pu.QuadPart);
            if (total_delta > 0) {
                g_resources.cpu_percent = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
                g_resources.cpu_available = true;
            }
        }
        g_resources.last_idle = idle; g_resources.last_kernel = kernel; g_resources.last_user = user; g_resources.has_cpu_sample = true;
    }

    MEMORYSTATUSEX ms = {0};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        g_resources.memory_load = ms.dwMemoryLoad;
        g_resources.memory_available = true;
    }

    TerminalPane *p = active_pane();
    const char *cwd = pane_cwd(p);
    char root[MAX_PATH] = {0};
    if (cwd[0]) {
        strncpy(root, cwd, MAX_PATH - 1);
        PathStripToRootA(root);
    }
    ULARGE_INTEGER free_to_caller = {0}, total = {0}, free_total = {0};
    wchar_t *wroot = root[0] ? u8_to_u16(root, NULL) : NULL;
    if (GetDiskFreeSpaceExW(wroot, &free_to_caller, &total, &free_total)) {
        g_resources.disk_free = free_to_caller.QuadPart;
        g_resources.disk_total = total.QuadPart;
        g_resources.disk_available = total.QuadPart > 0;
    }
    str_free(wroot);
}

static void update_git_status(void) {
    TerminalPane *p = active_pane();
    const char *cwd = pane_cwd(p);
    memset(&g_git, 0, sizeof(g_git));
    if (!cwd || !cwd[0]) return;
    char cur[MAX_PATH];
    strncpy(cur, cwd, MAX_PATH - 1);
    cur[MAX_PATH - 1] = '\0';
    while (cur[0]) {
        char git_dir[MAX_PATH];
        _snprintf(git_dir, MAX_PATH, "%s\\.git", cur);
        if (PathFileExistsA(git_dir)) {
            strncpy(g_git.repo_root, cur, MAX_PATH - 1);
            strncpy(g_git.project, basename_const(cur), sizeof(g_git.project) - 1);
            char head_path[MAX_PATH];
            _snprintf(head_path, MAX_PATH, "%s\\HEAD", git_dir);
            FILE *f = fopen(head_path, "r");
            if (f) {
                char line[256] = {0};
                if (fgets(line, sizeof(line), f)) {
                    char *nl = strpbrk(line, "\r\n");
                    if (nl) *nl = '\0';
                    const char *prefix = "ref: refs/heads/";
                    if (strncmp(line, prefix, strlen(prefix)) == 0) strncpy(g_git.branch, line + strlen(prefix), sizeof(g_git.branch) - 1);
                    else if (line[0]) strncpy(g_git.branch, line, 12);
                }
                fclose(f);
            }
            g_git.inside_repo = true;
            return;
        }
        if (PathIsRootA(cur)) break;
        PathRemoveFileSpecA(cur);
    }
    strncpy(g_git.project, basename_const(cwd), sizeof(g_git.project) - 1);
}

static void refresh_runtime_status(bool force) {
    DWORD now = GetTickCount();
    TerminalPane *p = active_pane();
    pane_sync_cwd(p);
    update_resources();
    if (force || now - g_git.last_tick > 2000) {
        update_git_status();
        g_git.last_tick = now;
    }
}

static void compact_path(const char *path, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;
    const int max_chars = 56;
    int len = (int)strlen(path);
    if (len <= max_chars) {
        strncpy(out, path, out_size - 1);
        return;
    }
    const char *base = basename_const(path);
    _snprintf(out, out_size, "... / %s", base && base[0] ? base : path + len - max_chars + 6);
}

static void screen_line_to_ascii(const ScreenBuffer *sb, int logical_row, char *out, int out_size) {
    if (!sb || !out || out_size <= 0) return;
    out[0] = '\0';
    int total = sb->scrollback_count + sb->rows;
    if (logical_row < 0 || logical_row >= total) return;
    int oi = 0;
    for (int c = 0; c < sb->cols && oi < out_size - 1; ++c) {
        const ScreenCell *cell = NULL;
        if (logical_row < sb->scrollback_count) cell = screen_scrollback_line_from_oldest((ScreenBuffer *)sb, logical_row, c);
        else cell = &sb->cells[(logical_row - sb->scrollback_count) * sb->cols + c];
        if (!cell || cell->wide_cont) continue;
        uint32_t ch = cell->ch ? cell->ch : ' ';
        out[oi++] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
    }
    while (oi > 0 && out[oi - 1] == ' ') oi--;
    out[oi] = '\0';
}

static void update_search_matches(void) {
    g_search.match_count = 0;
    TerminalPane *p = active_pane();
    if (!p || !g_search.query[0]) {
        g_search.active_match = 0;
        return;
    }
    char line[4096];
    int total = p->screen.scrollback_count + p->screen.rows;
    EnterCriticalSection(&g_lock);
    for (int r = 0; r < total && g_search.match_count < 999; ++r) {
        screen_line_to_ascii(&p->screen, r, line, sizeof(line));
        const char *pos = line;
        while ((pos = strstr(pos, g_search.query)) != NULL) {
            g_search.match_count++;
            pos += strlen(g_search.query);
        }
    }
    LeaveCriticalSection(&g_lock);
    if (g_search.match_count <= 0) g_search.active_match = 0;
    else if (g_search.active_match >= g_search.match_count) g_search.active_match = 0;
}

static void draw_status_dot(Renderer *r, float x, float y, uint32_t color) {
    D2D1_ELLIPSE e = {{x, y}, 3.5f, 3.5f};
    r->fg_brush->SetColor(d2d_rgb(color));
    r->render_target->FillEllipse(e, r->fg_brush);
}

static void draw_ui_dot(Renderer *r, float x, float y, float radius, uint32_t color) {
    D2D1_ELLIPSE e = {{x, y}, radius, radius};
    r->bg_brush->SetColor(d2d_rgb(color));
    r->render_target->FillEllipse(e, r->bg_brush);
}

static uint32_t pane_status_color(const TerminalPane *p) {
    if (!p || !p->initialized) return g_theme.red;
    if (p->use_pty && !pty_is_alive((PtySession *)&p->pty)) return g_theme.red;
    if (p->use_pty) return g_theme.green;
    return g_theme.green;
}

static void format_pane_label(const TerminalPane *p, char *out, int out_size) {
    const char *cwd = pane_cwd(p);
    const char *base = cwd && cwd[0] ? basename_const(cwd) : "";
    if (base && base[0]) _snprintf(out, out_size, "%s · %s", shell_label(), base);
    else _snprintf(out, out_size, "%s", shell_label());
}

static void draw_chrome(Renderer *r) {
    if (!r || !r->render_target || !r->fg_brush || !r->bg_brush) return;
    RECT client; GetClientRect(g_hwnd, &client);
    memset(&g_hits, 0, sizeof(g_hits));
    TerminalTab *tab = active_tab();
    TerminalPane *pane = active_pane();
    refresh_runtime_status(false);

    RECT title = {client.left, client.top, client.right, client.top + WSH_UI_TITLE_H};
    RECT toolbar = {client.left, title.bottom, client.right, title.bottom + WSH_UI_TOOLBAR_H};
    RECT sidebar = {client.left, toolbar.bottom, client.left + (g_sidebar_visible ? WSH_UI_SIDEBAR_W : 0), client.bottom - WSH_UI_STATUS_H};
    RECT term_header = {sidebar.right, toolbar.bottom, client.right, toolbar.bottom + WSH_UI_TERM_HEADER_H};
    RECT status = {client.left, client.bottom - WSH_UI_STATUS_H, client.right, client.bottom};
    RECT body = {sidebar.right, term_header.bottom, client.right, status.top};

    draw_rect(r, &title, g_theme.bg_card);
    draw_rect(r, &toolbar, g_theme.bg_surface);
    draw_rect(r, &status, g_theme.accent);
    if (g_sidebar_visible) draw_rect(r, &sidebar, g_theme.bg_surface);
    draw_rect(r, &term_header, g_theme.bg_surface);
    draw_line(r, (float)title.left, (float)title.bottom - 0.5f, (float)title.right, (float)title.bottom - 0.5f, g_theme.border);
    draw_line(r, (float)toolbar.left, (float)toolbar.bottom - 0.5f, (float)toolbar.right, (float)toolbar.bottom - 0.5f, g_theme.border);
    if (g_sidebar_visible) draw_line(r, (float)sidebar.right - 0.5f, (float)sidebar.top, (float)sidebar.right - 0.5f, (float)sidebar.bottom, g_theme.border);
    draw_line(r, (float)term_header.left, (float)term_header.bottom - 0.5f, (float)term_header.right, (float)term_header.bottom - 0.5f, g_theme.border);

    RECT brand = {16, 10, 56, 32};
    draw_text_u8(r, "Wsh", &brand, g_theme.text_primary);

    int actions_w = 150;
    int tx = 68;
    int tab_w = WSH_UI_TAB_W;
    int max_tab_right = client.right - actions_w - 12;
    if (g_tab_count > 1) {
        int available = max_tab_right - tx - (g_tab_count - 1) * 6;
        if (available > 0) {
            int fit = available / g_tab_count;
            if (fit < tab_w) tab_w = fit < 116 ? 116 : fit;
        }
    }
    g_hits.tab_count = g_tab_count;
    for (int i = 0; i < g_tab_count && i < WSH_MAX_TABS; ++i) {
        if (!g_tabs[i].initialized) continue;
        RECT tr = {tx, 7, tx + tab_w, WSH_UI_TITLE_H - 4};
        g_hits.tabs[i].rect = tr;
        g_hits.tabs[i].tab_index = i;
        draw_round_rect(r, &tr, 7.0f, (i == g_active_tab) ? g_theme.bg_surface : g_theme.bg_card);
        draw_round_border(r, &tr, 7.0f, (i == g_active_tab) ? g_theme.border_mid : g_theme.border);
        TerminalPane *tp = (g_tabs[i].pane_count > 0) ? &g_tabs[i].panes[g_tabs[i].active_pane] : NULL;
        draw_status_dot(r, (float)tr.left + 14.0f, (float)tr.top + 17.0f, pane_status_color(tp));
        char label[160]; format_pane_label(tp, label, sizeof(label));
        RECT lr = {tr.left + 24, tr.top + 7, tr.right - 28, tr.bottom - 4};
        draw_text_u8(r, label, &lr, (i == g_active_tab) ? g_theme.text_primary : g_theme.text_secondary);
        RECT cr = {tr.right - 25, tr.top + 7, tr.right - 7, tr.top + 25};
        g_hits.tab_close[i] = cr;
        float cx = ((float)cr.left + (float)cr.right) * 0.5f;
        float cy = ((float)cr.top + (float)cr.bottom) * 0.5f;
        draw_line(r, cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f, g_theme.text_faint);
        draw_line(r, cx + 4.0f, cy - 4.0f, cx - 4.0f, cy + 4.0f, g_theme.text_faint);
        tx += tab_w + 6;
    }
    g_hits.add_tab = {tx + 4, 10, tx + 30, 36};
    draw_round_rect(r, &g_hits.add_tab, 7.0f, g_theme.bg_hover);
    draw_round_border(r, &g_hits.add_tab, 7.0f, g_theme.border_mid);
    RECT add_text = {g_hits.add_tab.left + 8, g_hits.add_tab.top + 4, g_hits.add_tab.right, g_hits.add_tab.bottom};
    draw_text_u8(r, "+", &add_text, g_theme.text_primary);

    int bx = client.right - 148;
    g_hits.split = {bx, 8, bx + 28, 36}; draw_round_rect(r, &g_hits.split, 7.0f, g_theme.bg_card); RECT split_text = {bx + 9, 13, bx + 28, 34}; draw_text_u8(r, "S", &split_text, g_theme.text_secondary); bx += 32;
    g_hits.search = {bx, 8, bx + 28, 36}; draw_round_rect(r, &g_hits.search, 7.0f, g_theme.bg_card); RECT search_text = {bx + 9, 13, bx + 28, 34}; draw_text_u8(r, "F", &search_text, g_theme.text_secondary); bx += 32;
    g_hits.settings = {bx, 8, bx + 28, 36}; draw_round_rect(r, &g_hits.settings, 7.0f, g_theme.bg_card); RECT settings_text = {bx + 9, 13, bx + 28, 34}; draw_text_u8(r, "*", &settings_text, g_theme.text_secondary); bx += 32;
    g_hits.sidebar_toggle = {bx, 8, bx + 28, 36}; draw_round_rect(r, &g_hits.sidebar_toggle, 7.0f, g_theme.bg_card); RECT side_text = {bx + 9, 13, bx + 28, 34}; draw_text_u8(r, g_sidebar_visible ? "<" : ">", &side_text, g_theme.text_secondary);

    char path[256]; compact_path(pane_cwd(pane), path, sizeof(path));
    RECT path_box = {toolbar.left + 14, toolbar.top + 7, toolbar.right - 132, toolbar.bottom - 7};
    draw_round_rect(r, &path_box, 10.0f, g_theme.bg_base);
    draw_round_border(r, &path_box, 10.0f, g_theme.border_mid);
    draw_ui_dot(r, (float)path_box.left + 13.0f, (float)path_box.top + 13.0f, 3.5f, g_theme.accent);
    RECT path_text = {path_box.left + 24, path_box.top + 5, path_box.right - 10, path_box.bottom};
    draw_text_u8(r, path, &path_text, g_theme.text_secondary);
    RECT badge = {toolbar.right - 116, toolbar.top + 9, toolbar.right - 16, toolbar.bottom - 9};
    draw_round_rect(r, &badge, 10.0f, g_theme.bg_base);
    draw_round_border(r, &badge, 10.0f, g_theme.border_mid);
    RECT badge_text = {badge.left + 13, badge.top + 3, badge.right - 10, badge.bottom};
    draw_text_u8(r, is_process_elevated() ? "elevated" : "local", &badge_text, g_theme.green);

    char header[512];
    DWORD pid = pane && pane->use_pty ? pane->pty.pid : GetCurrentProcessId();
    const char *state = (pane && pane->use_pty && !pty_is_alive(&pane->pty)) ? "closed" : "idle";
    _snprintf(header, sizeof(header), "%s · PID %lu · %s · %s", shell_label(), (unsigned long)pid, path[0] ? path : "-", state);
    RECT hr = {term_header.left + 14, term_header.top + 7, term_header.right - 14, term_header.bottom};
    draw_text_u8(r, header, &hr, g_theme.text_secondary);

    if (g_sidebar_visible) {
        RECT sr = {sidebar.left + 14, sidebar.top + 12, sidebar.right - 10, sidebar.top + 30};
        draw_text_u8(r, "SESSIONS", &sr, g_theme.text_faint);
        int y = sidebar.top + 36;
        if (tab) {
            for (int i = 0; i < tab->pane_count; ++i) {
                RECT item = {sidebar.left + 8, y, sidebar.right - 8, y + 26};
                if (i == tab->active_pane) {
                    draw_round_rect(r, &item, 7.0f, g_theme.bg_hover);
                    RECT rail = {item.left, item.top + 5, item.left + 3, item.bottom - 5};
                    draw_round_rect(r, &rail, 2.0f, g_theme.accent);
                }
                draw_status_dot(r, (float)item.left + 12.0f, (float)item.top + 13.0f, pane_status_color(&tab->panes[i]));
                char label[160]; format_pane_label(&tab->panes[i], label, sizeof(label));
                RECT ir = {item.left + 24, item.top + 5, item.right - 6, item.bottom};
                draw_text_u8(r, label, &ir, i == tab->active_pane ? g_theme.text_primary : g_theme.text_secondary);
                y += 28;
            }
        }
        y += 10;
        RECT rr = {sidebar.left + 14, y, sidebar.right - 10, y + 18};
        draw_text_u8(r, "RESOURCES", &rr, g_theme.text_faint); y += 24;
        char metric[128];
        if (g_resources.cpu_available) { _snprintf(metric, sizeof(metric), "CPU %.0f%%", g_resources.cpu_percent); RECT mr={sidebar.left+14,y,sidebar.right-10,y+18}; draw_text_u8(r, metric, &mr, g_theme.text_secondary); y+=20; }
        if (g_resources.memory_available) { _snprintf(metric, sizeof(metric), "RAM %lu%%", (unsigned long)g_resources.memory_load); RECT mr={sidebar.left+14,y,sidebar.right-10,y+18}; draw_text_u8(r, metric, &mr, g_theme.text_secondary); y+=20; }
        if (g_resources.disk_available) { double used = 100.0 * (double)(g_resources.disk_total - g_resources.disk_free) / (double)g_resources.disk_total; _snprintf(metric, sizeof(metric), "Disk %.0f%%", used); RECT mr={sidebar.left+14,y,sidebar.right-10,y+18}; draw_text_u8(r, metric, &mr, g_theme.text_secondary); y+=20; }
        y += 10;
        RECT ar = {sidebar.left + 14, y, sidebar.right - 10, y + 18};
        draw_text_u8(r, "ACTIONS", &ar, g_theme.text_faint); y += 24;
        g_hits.duplicate = {sidebar.left + 14, y, sidebar.right - 14, y + 22};
        draw_round_rect(r, &g_hits.duplicate, 6.0f, g_theme.bg_card);
        RECT dup_text = {g_hits.duplicate.left + 10, g_hits.duplicate.top + 3, g_hits.duplicate.right - 8, g_hits.duplicate.bottom};
        draw_text_u8(r, "Duplicate pane", &dup_text, g_theme.text_secondary); y += 26;
        g_hits.kill = {sidebar.left + 14, y, sidebar.right - 14, y + 22};
        draw_round_rect(r, &g_hits.kill, 6.0f, g_theme.bg_card);
        RECT kill_text = {g_hits.kill.left + 10, g_hits.kill.top + 3, g_hits.kill.right - 8, g_hits.kill.bottom};
        draw_text_u8(r, "Kill process", &kill_text, pane_has_running_process(pane) ? g_theme.red : g_theme.text_faint);
    }

    if (g_search.open) {
        RECT overlay = {body.right - 320, body.top + 10, body.right - 18, body.top + 42};
        draw_round_rect(r, &overlay, 10.0f, g_theme.bg_card);
        draw_round_border(r, &overlay, 10.0f, g_theme.border_mid);
        draw_ui_dot(r, (float)overlay.left + 14.0f, (float)overlay.top + 16.0f, 3.5f, g_theme.accent);
        char q[220];
        if (g_search.query[0]) _snprintf(q, sizeof(q), "Search: %s  %d match%s", g_search.query, g_search.match_count, g_search.match_count == 1 ? "" : "es");
        else _snprintf(q, sizeof(q), "Search:");
        RECT qr = {overlay.left + 26, overlay.top + 8, overlay.right - 10, overlay.bottom};
        draw_text_u8(r, q, &qr, g_theme.text_primary);
    }

    char clock_buf[64];
    SYSTEMTIME st; GetLocalTime(&st);
    _snprintf(clock_buf, sizeof(clock_buf), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    char stbuf[512];
    char branch[160] = {0};
    if (g_git.inside_repo && g_git.branch[0]) _snprintf(branch, sizeof(branch), "git:%s  ", g_git.branch);
    _snprintf(stbuf, sizeof(stbuf), "%s%s  %s  UTF-8  %dx%d  pane %d/%d  %s",
              branch, g_git.project[0] ? g_git.project : basename_const(pane_cwd(pane)),
              shell_label(), pane ? pane->screen.cols : 0, pane ? pane->screen.rows : 0,
              tab ? tab->active_pane + 1 : 0, tab ? tab->pane_count : 0, clock_buf);
    RECT str = {status.left + 14, status.top + 4, status.right - 12, status.bottom};
    draw_text_u8(r, stbuf, &str, 0xffffff);
}

/* ── Trigger AI reasoning update after input changes ───────────────────────── */

static void trigger_ai_reasoning(TerminalPane *p) {
    if (!p || p->use_pty) return;
    if (!p->shell.ai_enabled) return;
    /* Check if REPL's line changed and trigger async analysis */
    if (repl_is_reasoning_dirty(&p->repl)) {
        const char *line = repl_get_line(&p->repl);
        if (line && line[0]) {
            wsh_ai_trigger_analysis(&p->shell, line);
        }
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            if (!g_renderer.render_target || !g_renderer.fg_brush || !g_renderer.bg_brush) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            update_native_scrollbar(hwnd);
            EnterCriticalSection(&g_lock);
            renderer_begin_frame(&g_renderer);
            draw_chrome(&g_renderer);
            g_renderer.search_active = g_search.open && g_search.query[0];
            strncpy(g_renderer.search_query, g_search.query, sizeof(g_renderer.search_query) - 1);
            g_renderer.search_query[sizeof(g_renderer.search_query) - 1] = '\0';
            TerminalTab *tab = active_tab();
            /* Update proactive reasoning overlay from AI state */
            {
                TerminalPane *ap = active_pane();
                if (ap && ap->initialized && !ap->use_pty) {
                    ShellContext *sh = &ap->shell;
                    if (sh->ai_enabled && sh->ai_state) {
                        const char *input = repl_get_line(&ap->repl);
                        if (input && input[0]) {
                            /* Line 1: show what the user typed */
                            char line_buf[260];
                            _snprintf(line_buf, sizeof(line_buf), "user input: %s", input);
                            wchar_t *winput = u8_to_u16(line_buf, NULL);
                            if (winput) {
                                wcsncpy(g_renderer.reasoning_line1, winput, 255);
                                g_renderer.reasoning_line1[255] = L'\0';
                                str_free(winput);
                            }
                            /* Line 2: friendly AI reasoning (may lag by one keystroke) */
                            const char *reason = wsh_ai_get_reasoning(sh);
                            if (reason && reason[0]) {
                                char reason_buf[260];
                                _snprintf(reason_buf, sizeof(reason_buf), "reason: %s", reason);
                                wchar_t *wreason = u8_to_u16(reason_buf, NULL);
                                if (wreason) {
                                    wcsncpy(g_renderer.reasoning_line2, wreason, 255);
                                    g_renderer.reasoning_line2[255] = L'\0';
                                    str_free(wreason);
                                }
                            } else {
                                g_renderer.reasoning_line2[0] = L'\0';
                            }
                            g_renderer.reasoning_active = true;
                        } else {
                            g_renderer.reasoning_active = false;
                        }
                    } else {
                        g_renderer.reasoning_active = false;
                    }
                } else {
                    g_renderer.reasoning_active = false;
                }
            }
            if (tab) {
                for (int i = 0; i < tab->pane_count; ++i) {
                    TerminalPane *p = &tab->panes[i];
                    bool active = (i == tab->active_pane);
                    bool cursor_visible_in_view = (p->screen.viewport_offset == 0);
                    renderer_paint_region(&g_renderer, &p->screen, &p->rect, active,
                                          cursor_visible_in_view, p->screen.cursor_x, p->screen.cursor_y);
                }
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
            if (wParam == 2) {
                refresh_runtime_status(false);
                TerminalPane *tp = active_pane();
                if (tp && tp->initialized && !tp->use_pty)
                    shell_scheduler_tick(&tp->shell);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_SETFOCUS: { TerminalPane *p = active_pane(); if (p) p->screen.cursor_visible = true; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        case WM_KILLFOCUS: { TerminalPane *p = active_pane(); if (p) p->screen.cursor_visible = false; InvalidateRect(hwnd, NULL, FALSE); return 0; }

        case WM_KEYDOWN: {
            TerminalPane *p = active_pane();
            if (!p) return 0;
            bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

            if (ctrl && shift && wParam == 'T') { g_suppress_char = true; tab_add(); return 0; }
            if (ctrl && shift && wParam == 'D') { g_suppress_char = true; pane_add(); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (ctrl && shift && wParam == 'W') { g_suppress_char = true; pane_close_active(); return 0; }
            if (ctrl && shift && wParam == 'F') { g_suppress_char = true; g_search.open = true; g_search.query[0] = '\0'; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            TerminalTab *tab = active_tab();
            if (alt && (wParam == VK_LEFT || wParam == VK_UP)) { g_suppress_char = true; if (tab && tab->pane_count > 1) { tab->active_pane = (tab->active_pane + tab->pane_count - 1) % tab->pane_count; update_native_scrollbar(hwnd); refresh_runtime_status(true); InvalidateRect(hwnd, NULL, FALSE); } return 0; }
            if (alt && (wParam == VK_RIGHT || wParam == VK_DOWN)) { g_suppress_char = true; if (tab && tab->pane_count > 1) { tab->active_pane = (tab->active_pane + 1) % tab->pane_count; update_native_scrollbar(hwnd); refresh_runtime_status(true); InvalidateRect(hwnd, NULL, FALSE); } return 0; }
            if (g_search.open && wParam == VK_RETURN) { g_suppress_char = true; update_search_matches(); if (g_search.match_count > 0) g_search.active_match = (g_search.active_match + 1) % g_search.match_count; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == VK_ESCAPE && g_search.open) { g_suppress_char = true; g_search.open = false; InvalidateRect(hwnd, NULL, FALSE); return 0; }

            InputEvent ev = input_translate(wParam, 0, lParam, p->screen.app_cursor_keys);
            g_suppress_char = input_suppress_char(wParam, lParam);

            switch (ev.action) {
                case INPUT_COPY: {
                    TerminalPane *cp = active_pane();
                    if (cp && g_renderer.sel_valid && OpenClipboard(hwnd)) {
                        int sr = g_renderer.sel_start_row, sc = g_renderer.sel_start_col;
                        int er = g_renderer.sel_end_row,   ec = g_renderer.sel_end_col;
                        if (sr > er || (sr == er && sc > ec)) { int tr=sr,tc=sc; sr=er;sc=ec; er=tr;ec=tc; }
                        char text[65536]; int ti = 0;
                        EnterCriticalSection(&g_lock);
                        for (int r = sr; r <= er && ti < 65520; r++) {
                            int c0 = (r == sr) ? sc : 0;
                            int c1 = (r == er) ? ec : cp->screen.cols - 1;
                            int last_ns = c0 - 1;
                            for (int c = c0; c <= c1; c++) {
                                const ScreenCell *cell = screen_visible_cell(&cp->screen, r, c);
                                if (cell && !cell->wide_cont && cell->ch > ' ') last_ns = c;
                            }
                            for (int c = c0; c <= last_ns && ti < 65512; c++) {
                                const ScreenCell *cell = screen_visible_cell(&cp->screen, r, c);
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
                        g_renderer.sel_valid = false;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                }
                case INPUT_PASTE:
                    if (OpenClipboard(hwnd)) {
                        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                        if (hd) {
                            const wchar_t *wtext = (const wchar_t *)GlobalLock(hd);
                            if (wtext) {
                                int u8len = 0;
                                char *text = wsh_utf16_to_utf8_clipboard(wtext, &u8len);
                                if (text) { if (p->use_pty) pty_write(&p->pty, text, u8len); else { repl_handle_input(&p->repl, text, u8len); trigger_ai_reasoning(p); } str_free(text); }
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
                case INPUT_NEXT_TAB:
                    if (g_tab_count > 1) { g_active_tab = (g_active_tab + 1) % g_tab_count; active_tab(); layout_panes(hwnd); refresh_runtime_status(true); InvalidateRect(hwnd, NULL, FALSE); }
                    break;
                case INPUT_PREV_TAB:
                    if (g_tab_count > 1) { g_active_tab = (g_active_tab + g_tab_count - 1) % g_tab_count; active_tab(); layout_panes(hwnd); refresh_runtime_status(true); InvalidateRect(hwnd, NULL, FALSE); }
                    break;
                case INPUT_CHAR:
                    if (ev.len > 0) {
                        if (p->screen.viewport_offset) { EnterCriticalSection(&g_lock); p->screen.viewport_offset = 0; screen_mark_dirty_all(&p->screen); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); }
                        if (p->use_pty) pty_write(&p->pty, ev.bytes, ev.len);
                        else {
                            if (!repl_handle_input(&p->repl, ev.bytes, ev.len)) PostQuitMessage(0);
                            trigger_ai_reasoning(p);
                        }
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
            if (g_search.open) {
                if (ch == 27) g_search.open = false;
                else if (ch == '\b') { size_t n = strlen(g_search.query); if (n > 0) g_search.query[n - 1] = '\0'; }
                else if (ch >= 32 && ch < 127) { size_t n = strlen(g_search.query); if (n + 1 < sizeof(g_search.query)) { g_search.query[n] = (char)ch; g_search.query[n + 1] = '\0'; } }
                update_search_matches();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (ch == '\r') return 0;
            InputEvent ev = input_translate(0, ch, lParam, p->screen.app_cursor_keys);
            if (ev.action == INPUT_CHAR && ev.len > 0) {
                if (p->screen.viewport_offset) { EnterCriticalSection(&g_lock); p->screen.viewport_offset = 0; screen_mark_dirty_all(&p->screen); LeaveCriticalSection(&g_lock); update_native_scrollbar(hwnd); }
                if (p->use_pty) pty_write(&p->pty, ev.bytes, ev.len);
                else {
                    if (!repl_handle_input(&p->repl, ev.bytes, ev.len)) PostQuitMessage(0);
                    trigger_ai_reasoning(p);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            POINT pt = {mx, my};
            for (int i = 0; i < g_hits.tab_count; ++i) {
                if (PtInRect(&g_hits.tab_close[i], pt)) { ReleaseCapture(); tab_close_index(i); return 0; }
                if (PtInRect(&g_hits.tabs[i].rect, pt)) { g_active_tab = i; layout_panes(hwnd); update_native_scrollbar(hwnd); refresh_runtime_status(true); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            }
            if (PtInRect(&g_hits.add_tab, pt)) { ReleaseCapture(); tab_add(); return 0; }
            if (PtInRect(&g_hits.split, pt)) { ReleaseCapture(); pane_add(); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (PtInRect(&g_hits.search, pt)) { ReleaseCapture(); g_search.open = true; g_search.query[0] = '\0'; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (PtInRect(&g_hits.settings, pt)) { ReleaseCapture(); char cfg_path[MAX_PATH]; config_path(cfg_path, MAX_PATH); wchar_t *wcfg = u8_to_u16(cfg_path, NULL); if (wcfg) { ShellExecuteW(hwnd, L"open", wcfg, NULL, NULL, SW_SHOWNORMAL); str_free(wcfg); } return 0; }
            if (PtInRect(&g_hits.sidebar_toggle, pt)) { ReleaseCapture(); g_sidebar_visible = !g_sidebar_visible; layout_panes(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (PtInRect(&g_hits.duplicate, pt)) { ReleaseCapture(); pane_add(); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (PtInRect(&g_hits.kill, pt)) { ReleaseCapture(); TerminalPane *kp = active_pane(); if (kp && kp->use_pty && pane_has_running_process(kp)) pty_close(&kp->pty); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            int hit = pane_hit_test(mx, my);
            TerminalTab *tab = active_tab();
            if (tab && hit >= 0 && hit < tab->pane_count) tab->active_pane = hit;
            TerminalPane *p = active_pane();
            if (!p || !PtInRect(&p->rect, pt)) { ReleaseCapture(); return 0; }
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
                        if (txt) { if (p->use_pty) pty_write(&p->pty, txt, u8len); else { repl_handle_input(&p->repl, txt, u8len); trigger_ai_reasoning(p); } str_free(txt); }
                        GlobalUnlock(hd);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            TerminalTab *tab = active_tab();
            TerminalPane *p = active_pane(); if (!p) return 0;
            POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
            ScreenToClient(hwnd, &pt);
            int hit = pane_hit_test(pt.x, pt.y);
            if (tab && hit >= 0 && hit < tab->pane_count && PtInRect(&tab->panes[hit].rect, pt)) {
                tab->active_pane = hit;
                p = &tab->panes[hit];
            }
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

        case WM_WSH_EXEC_DONE: {
            int ti = (int)wParam;
            int pi = (int)lParam;
            if (ti >= 0 && ti < g_tab_count && pi >= 0 && pi < WSH_MAX_PANES) {
                TerminalTab *tab = &g_tabs[ti];
                if (tab->initialized && pi < tab->pane_count) {
                    TerminalPane *pane = &tab->panes[pi];
                    if (pane->initialized && !pane->shell.exit_requested) {
                        repl_show_prompt(&pane->repl);
                        update_native_scrollbar(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE:
            if (g_cfg.general.confirm_exit) {
                if (MessageBoxW(hwnd, L"Close Wsh?", L"Wsh", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            }
            DestroyWindow(hwnd); return 0;

        case WM_DESTROY:
            for (int i = 0; i < g_tab_count; ++i) tab_stop(&g_tabs[i]);
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

    if (!window_register_class(hInst)) return 1;
    WSH_LOG_DEBUG("window class registered");
    g_hwnd = window_create(hInst, &g_cfg, nShow);
    if (!g_hwnd) return 1;
    WSH_LOG_DEBUG("window created hwnd=%p", (void *)g_hwnd);
    apply_window_title(g_hwnd, &g_cfg);

    if (!renderer_init(&g_renderer, g_hwnd, &g_cfg)) return 1;
    WSH_LOG_DEBUG("renderer initialized");
    theme_material_cyber_dark(&g_theme);
    g_renderer.bg_color = renderer_rgb_to_color(g_theme.bg_base);
    g_renderer.fg_color = renderer_rgb_to_color(g_theme.text_primary);
    g_renderer.cursor_color = renderer_rgb_to_color(g_theme.accent);
    g_renderer.selection_color = renderer_rgb_to_color(0x27324a);
    g_renderer.reserved_left_px = g_sidebar_visible ? WSH_UI_SIDEBAR_W : 0;
    g_renderer.reserved_top_px = WSH_UI_TITLE_H + WSH_UI_TOOLBAR_H + WSH_UI_TERM_HEADER_H;
    g_renderer.reserved_bottom_px = WSH_UI_STATUS_H;

    RECT rc; terminal_view_rect(g_hwnd, &rc);
    g_tabs[0].panes[0].rect = rc;
    g_tab_count = 1;
    g_active_tab = 0;
    WSH_LOG_DEBUG("starting initial tab");
    tab_start(&g_tabs[0], NULL);
    WSH_LOG_DEBUG("initial tab started");
    layout_panes(g_hwnd);
    update_native_scrollbar(g_hwnd);
    refresh_runtime_status(true);
    SetTimer(g_hwnd, 2, 1000, NULL);
    ShowWindow(g_hwnd, nShow == 0 ? SW_SHOWNORMAL : nShow);
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    UpdateWindow(g_hwnd);
    WSH_LOG_DEBUG("window shown visible=%d", IsWindowVisible(g_hwnd) ? 1 : 0);

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
