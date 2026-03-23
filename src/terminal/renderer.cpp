/* renderer.c — Direct2D + DirectWrite terminal cell renderer */
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "renderer.h"
#include "screen.h"
#include "../platform/config.h"
#include "font.h"
#include "../core/str_util.h"
#include "../core/log.h"

/* ─── Color helpers ──────────────────────────────────────────────────────── */

Color4F renderer_rgb_to_color(uint32_t rgb) {
    Color4F c;
    c.r = ((rgb >> 16) & 0xFF) / 255.0f;
    c.g = ((rgb >>  8) & 0xFF) / 255.0f;
    c.b = ((rgb      ) & 0xFF) / 255.0f;
    c.a = 1.0f;
    return c;
}

static D2D1_COLOR_F color4f_to_d2d(Color4F c) {
    D2D1_COLOR_F d = { c.r, c.g, c.b, c.a };
    return d;
}

/* ─── Build 256-color palette ────────────────────────────────────────────── */

static void build_palette(Renderer *r, const Config *cfg) {
    /* 0-15: configurable ANSI colors */
    for (int i = 0; i < 16; i++) r->palette[i] = renderer_rgb_to_color(cfg->colors.ansi[i]);

    /* 16-231: 6x6x6 color cube */
    for (int i = 16; i < 232; i++) {
        int n = i - 16;
        int b = n % 6; n /= 6;
        int g = n % 6; n /= 6;
        int rd = n % 6;
        r->palette[i].r = rd ? (55 + rd * 40) / 255.0f : 0.0f;
        r->palette[i].g = g  ? (55 + g  * 40) / 255.0f : 0.0f;
        r->palette[i].b = b  ? (55 + b  * 40) / 255.0f : 0.0f;
        r->palette[i].a = 1.0f;
    }

    /* 232-255: grayscale ramp */
    for (int i = 232; i < 256; i++) {
        float v = (8 + (i - 232) * 10) / 255.0f;
        r->palette[i].r = r->palette[i].g = r->palette[i].b = v;
        r->palette[i].a = 1.0f;
    }

    r->bg_color       = renderer_rgb_to_color(cfg->colors.background);
    r->fg_color       = renderer_rgb_to_color(cfg->colors.foreground);
    r->cursor_color   = renderer_rgb_to_color(cfg->colors.cursor);
    r->selection_color= renderer_rgb_to_color(cfg->colors.selection);
}

/* ─── Color resolution ───────────────────────────────────────────────────── */

Color4F renderer_resolve_fg(const Renderer *r, const ScreenCell *cell) {
    if (cell->attr.fg_idx == 0xFF) return renderer_rgb_to_color(cell->attr.fg_rgb);
    if (cell->attr.dim) {
        Color4F c = r->palette[cell->attr.fg_idx];
        c.r *= 0.5f; c.g *= 0.5f; c.b *= 0.5f;
        return c;
    }
    return r->palette[cell->attr.fg_idx < 256 ? cell->attr.fg_idx : 7];
}

Color4F renderer_resolve_bg(const Renderer *r, const ScreenCell *cell) {
    if (cell->attr.bg_idx == 0xFF) return renderer_rgb_to_color(cell->attr.bg_rgb);
    if (cell->attr.bg_idx == 0)    return r->bg_color;
    return r->palette[cell->attr.bg_idx < 256 ? cell->attr.bg_idx : 0];
}

/* ─── Create/recreate render target ─────────────────────────────────────── */

static bool create_render_target(Renderer *r) {
    if (r->render_target) {
        r->render_target->lpVtbl->Release(r->render_target);
        r->render_target = NULL;
    }

    RECT rc; GetClientRect(r->hwnd, &rc);
    D2D1_SIZE_U size = { (UINT32)(rc.right - rc.left), (UINT32)(rc.bottom - rc.top) };
    if (size.width == 0) size.width = 1;
    if (size.height == 0) size.height = 1;

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props;
    hwnd_props.hwnd            = r->hwnd;
    hwnd_props.pixelSize       = size;
    hwnd_props.presentOptions  = D2D1_PRESENT_OPTIONS_NONE;

    D2D1_RENDER_TARGET_PROPERTIES rt_props;
    rt_props.type        = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt_props.pixelFormat.format    = DXGI_FORMAT_UNKNOWN;
    rt_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    rt_props.dpiX = rt_props.dpiY = 0; /* Use system DPI */
    rt_props.usage       = D2D1_RENDER_TARGET_USAGE_NONE;
    rt_props.minLevel    = D2D1_FEATURE_LEVEL_DEFAULT;

    HRESULT hr = r->d2d_factory->lpVtbl->CreateHwndRenderTarget(
        r->d2d_factory, &rt_props, &hwnd_props, &r->render_target);
    if (FAILED(hr)) { wsh_log("CreateHwndRenderTarget failed: 0x%08X", hr); return false; }

    /* Recreate brushes */
    if (r->fg_brush) { r->fg_brush->lpVtbl->Release(r->fg_brush); r->fg_brush = NULL; }
    if (r->bg_brush) { r->bg_brush->lpVtbl->Release(r->bg_brush); r->bg_brush = NULL; }
    if (r->cursor_brush) { r->cursor_brush->lpVtbl->Release(r->cursor_brush); r->cursor_brush = NULL; }

    D2D1_COLOR_F col = color4f_to_d2d(r->fg_color);
    r->render_target->lpVtbl->CreateSolidColorBrush(r->render_target, &col, NULL, &r->fg_brush);
    col = color4f_to_d2d(r->bg_color);
    r->render_target->lpVtbl->CreateSolidColorBrush(r->render_target, &col, NULL, &r->bg_brush);
    col = color4f_to_d2d(r->cursor_color);
    r->render_target->lpVtbl->CreateSolidColorBrush(r->render_target, &col, NULL, &r->cursor_brush);

    return true;
}

/* ─── renderer_init ──────────────────────────────────────────────────────── */

bool renderer_init(Renderer *r, HWND hwnd, const Config *cfg) {
    memset(r, 0, sizeof(*r));
    r->hwnd    = hwnd;
    r->cfg     = cfg;
    r->padding_x = 4;
    r->padding_y = 4;

    /* Get DPI */
    HDC hdc = GetDC(hwnd);
    r->dpi = hdc ? (float)GetDeviceCaps(hdc, LOGPIXELSX) : 96.0f;
    if (hdc) ReleaseDC(hwnd, hdc);

    /* Build color palette */
    build_palette(r, cfg);

    /* Create D2D1 factory */
    D2D1_FACTORY_OPTIONS opts = { D2D1_DEBUG_LEVEL_NONE };
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   &IID_ID2D1Factory, &opts,
                                   (void **)&r->d2d_factory);
    if (FAILED(hr)) { wsh_log("D2D1CreateFactory failed: 0x%08X", hr); return false; }

    /* Create render target */
    if (!create_render_target(r)) return false;

    /* Init font */
    if (!font_init(&r->font, cfg->font.family, cfg->font.size, r->dpi)) {
        wsh_log("font_init failed");
        return false;
    }

    r->cell_w = r->font.cell_width;
    r->cell_h = r->font.cell_height;

    /* Cursor config */
    r->cursor_style   = cfg->cursor.style;
    r->cursor_visible = true;
    r->cursor_blink_state = true;

    /* Tab bar */
    r->tab_bar_height = cfg->tabs.enabled ? 32 : 0;

    /* Start blink timer */
    if (cfg->cursor.blink) {
        r->blink_timer_id = SetTimer(hwnd, 1, cfg->cursor.blink_rate_ms, NULL);
    }

    /* Compute initial grid size */
    RECT rc; GetClientRect(hwnd, &rc);
    renderer_resize(r, rc.right - rc.left, rc.bottom - rc.top);

    return true;
}

/* ─── renderer_resize ────────────────────────────────────────────────────── */

void renderer_resize(Renderer *r, int width_px, int height_px) {
    if (width_px < 1) width_px = 1;
    if (height_px < 1) height_px = 1;

    /* Resize render target */
    if (r->render_target) {
        D2D1_SIZE_U size = { (UINT32)width_px, (UINT32)height_px };
        r->render_target->lpVtbl->Resize(r->render_target, &size);
    }

    int usable_w = width_px  - 2 * r->padding_x;
    int usable_h = height_px - 2 * r->padding_y - r->tab_bar_height;
    if (usable_w < 0) usable_w = 0;
    if (usable_h < 0) usable_h = 0;

    r->cols = r->cell_w > 0 ? (int)(usable_w / r->cell_w) : 80;
    r->rows = r->cell_h > 0 ? (int)(usable_h / r->cell_h) : 24;
    if (r->cols < 1)  r->cols = 1;
    if (r->rows < 1)  r->rows = 1;
}

/* ─── renderer_paint ─────────────────────────────────────────────────────── */

void renderer_paint(Renderer *r, const ScreenBuffer *sb,
                    bool cursor_shown, int cursor_x, int cursor_y) {
    if (!r->render_target || !r->font.fmt_normal) return;

    r->render_target->lpVtbl->BeginDraw(r->render_target);

    /* Clear background */
    D2D1_COLOR_F bg = color4f_to_d2d(r->bg_color);
    r->render_target->lpVtbl->Clear(r->render_target, &bg);

    int rows = sb->rows < r->rows ? sb->rows : r->rows;
    int cols = sb->cols < r->cols ? sb->cols : r->cols;

    float ox = (float)r->padding_x;
    float oy = (float)(r->padding_y + r->tab_bar_height);

    wchar_t wch[4]; /* temp buffer for one char */

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            const ScreenCell *cell = NULL;

            /* Scrollback vs. live buffer */
            if (sb->viewport_offset > 0) {
                int sb_line = sb->viewport_offset - (rows - row);
                if (sb_line >= 0 && sb_line < sb->scrollback_count) {
                    cell = screen_scrollback_line((ScreenBuffer *)sb, sb_line, col);
                }
            }
            if (!cell) {
                cell = &sb->cells[row * sb->cols + col];
            }

            float cx = ox + col * r->cell_w;
            float cy = oy + row * r->cell_h;
            D2D1_RECT_F cell_rect = { cx, cy, cx + r->cell_w, cy + r->cell_h };

            /* Resolve colors */
            Color4F fg = renderer_resolve_fg(r, cell);
            Color4F bg_c = renderer_resolve_bg(r, cell);

            /* Reverse video */
            if (cell->attr.reverse) { Color4F tmp = fg; fg = bg_c; bg_c = tmp; }

            /* Draw background (only if not default) */
            if (bg_c.r != r->bg_color.r || bg_c.g != r->bg_color.g || bg_c.b != r->bg_color.b) {
                D2D1_COLOR_F d2bg = color4f_to_d2d(bg_c);
                r->bg_brush->lpVtbl->SetColor(r->bg_brush, &d2bg);
                r->render_target->lpVtbl->FillRectangle(r->render_target, &cell_rect, r->bg_brush);
            }

            /* Skip wide continuation cells and spaces */
            if (cell->wide_cont || !cell->ch || cell->ch == ' ') goto draw_cursor;

            /* Encode codepoint to UTF-16 */
            if (cell->ch < 0x10000) {
                wch[0] = (wchar_t)cell->ch; wch[1] = 0;
            } else {
                /* Surrogate pair */
                uint32_t cp = cell->ch - 0x10000;
                wch[0] = (wchar_t)(0xD800 | (cp >> 10));
                wch[1] = (wchar_t)(0xDC00 | (cp & 0x3FF));
                wch[2] = 0;
            }

            /* Pick format */
            IDWriteTextFormat *fmt = r->font.fmt_normal;
            if (cell->attr.bold && cell->attr.italic)  fmt = r->font.fmt_bold_italic;
            else if (cell->attr.bold)                   fmt = r->font.fmt_bold;
            else if (cell->attr.italic)                 fmt = r->font.fmt_italic;

            /* Create text layout */
            {
                int wlen = cell->ch >= 0x10000 ? 2 : 1;
                IDWriteTextLayout *layout = NULL;
                if (r->font.factory) {
                    r->font.factory->lpVtbl->CreateTextLayout(
                        r->font.factory, wch, (UINT32)wlen, fmt,
                        r->cell_w * (cell->wide ? 2.0f : 1.0f),
                        r->cell_h, &layout);
                }
                if (layout) {
                    D2D1_POINT_2F origin = { cx, cy };
                    D2D1_COLOR_F d2fg = color4f_to_d2d(fg);
                    r->fg_brush->lpVtbl->SetColor(r->fg_brush, &d2fg);
                    r->render_target->lpVtbl->DrawTextLayout(
                        r->render_target, origin, layout,
                        r->fg_brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                    layout->lpVtbl->Release(layout);
                }
            }

            /* Underline */
            if (cell->attr.underline) {
                float uy = cy + r->cell_h - 2.0f;
                D2D1_POINT_2F p1 = { cx, uy }, p2 = { cx + r->cell_w, uy };
                D2D1_COLOR_F d2fg = color4f_to_d2d(fg);
                r->fg_brush->lpVtbl->SetColor(r->fg_brush, &d2fg);
                r->render_target->lpVtbl->DrawLine(r->render_target, p1, p2, r->fg_brush, 1.0f, NULL);
            }

            draw_cursor:
            /* Draw cursor at this cell */
            if (cursor_shown && r->cursor_blink_state &&
                col == cursor_x && row == cursor_y && sb->cursor_visible) {
                D2D1_COLOR_F cur_col = color4f_to_d2d(r->cursor_color);
                r->cursor_brush->lpVtbl->SetColor(r->cursor_brush, &cur_col);
                switch (r->cursor_style) {
                    case CURSOR_BLOCK:
                        r->render_target->lpVtbl->FillRectangle(r->render_target, &cell_rect, r->cursor_brush);
                        break;
                    case CURSOR_BAR: {
                        D2D1_RECT_F bar = { cx, cy, cx + 2.0f, cy + r->cell_h };
                        r->render_target->lpVtbl->FillRectangle(r->render_target, &bar, r->cursor_brush);
                        break;
                    }
                    case CURSOR_UNDERLINE: {
                        D2D1_RECT_F uline = { cx, cy + r->cell_h - 2.0f, cx + r->cell_w, cy + r->cell_h };
                        r->render_target->lpVtbl->FillRectangle(r->render_target, &uline, r->cursor_brush);
                        break;
                    }
                }
            }
        }
    }

    HRESULT hr = r->render_target->lpVtbl->EndDraw(r->render_target, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        /* Device lost — recreate */
        create_render_target(r);
    }
}

/* ─── renderer_set_font ──────────────────────────────────────────────────── */

void renderer_set_font(Renderer *r, const wchar_t *family, float pt_size) {
    font_free(&r->font);
    font_init(&r->font, family, pt_size, r->dpi);
    r->cell_w = r->font.cell_width;
    r->cell_h = r->font.cell_height;
}

/* ─── renderer_update_dpi ────────────────────────────────────────────────── */

void renderer_update_dpi(Renderer *r, float dpi) {
    r->dpi = dpi;
    float pt = r->font.pt_size > 0 ? r->font.pt_size : (r->cfg ? r->cfg->font.size : 13.0f);
    font_resize(&r->font, pt, dpi);
    r->cell_w = r->font.cell_width;
    r->cell_h = r->font.cell_height;
    create_render_target(r);
}

/* ─── renderer_toggle_cursor_blink ──────────────────────────────────────── */

void renderer_toggle_cursor_blink(Renderer *r) {
    r->cursor_blink_state = !r->cursor_blink_state;
}

/* ─── renderer_destroy ───────────────────────────────────────────────────── */

void renderer_destroy(Renderer *r) {
    if (r->blink_timer_id) { KillTimer(r->hwnd, r->blink_timer_id); r->blink_timer_id = 0; }
    font_free(&r->font);
    if (r->cursor_brush)  { r->cursor_brush->lpVtbl->Release(r->cursor_brush);   r->cursor_brush = NULL; }
    if (r->bg_brush)      { r->bg_brush->lpVtbl->Release(r->bg_brush);           r->bg_brush = NULL; }
    if (r->fg_brush)      { r->fg_brush->lpVtbl->Release(r->fg_brush);           r->fg_brush = NULL; }
    if (r->render_target) { r->render_target->lpVtbl->Release(r->render_target); r->render_target = NULL; }
    if (r->d2d_factory)   { r->d2d_factory->lpVtbl->Release(r->d2d_factory);     r->d2d_factory = NULL; }
}
