/* font.c — compiled as C++ for COM/DirectWrite COM interop */
#include <windows.h>
#include <dwrite.h>
#include <string.h>
#include "font.h"
#include "../core/str_util.h"
#include "../core/log.h"

/* ─── Helper: create one IDWriteTextFormat ───────────────────────────────── */

static IDWriteTextFormat *make_fmt(IDWriteFactory *factory, const wchar_t *family,
                                   float pt, DWRITE_FONT_WEIGHT weight,
                                   DWRITE_FONT_STYLE style) {
    IDWriteTextFormat *fmt = NULL;
    HRESULT hr = factory->lpVtbl->CreateTextFormat(
        factory, family, NULL,
        weight, style, DWRITE_FONT_STRETCH_NORMAL,
        pt, L"en-US", &fmt);
    if (FAILED(hr)) {
        /* Fallback to Consolas */
        factory->lpVtbl->CreateTextFormat(
            factory, L"Consolas", NULL,
            weight, style, DWRITE_FONT_STRETCH_NORMAL,
            pt, L"en-US", &fmt);
    }
    if (fmt) {
        fmt->lpVtbl->SetWordWrapping(fmt, DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->lpVtbl->SetTextAlignment(fmt, DWRITE_TEXT_ALIGNMENT_LEADING);
        fmt->lpVtbl->SetParagraphAlignment(fmt, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return fmt;
}

/* ─── Measure cell size ──────────────────────────────────────────────────── */

static bool measure_cell(FontState *fs, float dpi) {
    if (!fs->factory || !fs->fmt_normal) return false;

    /* Lay out a reference character ('M' or 'W') to measure cell dimensions */
    IDWriteTextLayout *layout = NULL;
    HRESULT hr = fs->factory->lpVtbl->CreateTextLayout(
        fs->factory, L"W", 1, fs->fmt_normal, 10000.0f, 10000.0f, &layout);
    if (FAILED(hr) || !layout) return false;

    DWRITE_TEXT_METRICS metrics = {0};
    layout->lpVtbl->GetMetrics(layout, &metrics);

    /* Cell width = character advance width; use a fixed wide enough value */
    fs->cell_width  = metrics.width;
    fs->cell_height = metrics.height;

    /* Get line spacing for baseline */
    DWRITE_LINE_SPACING_METHOD lsm;
    float line_spacing = 0.0f, baseline = 0.0f;
    layout->lpVtbl->GetLineSpacing(layout, &lsm, &line_spacing, &baseline);
    fs->baseline = baseline;

    layout->lpVtbl->Release(layout);

    /* Ensure cell is at least 1px */
    if (fs->cell_width  < 1.0f) fs->cell_width  = 8.0f;
    if (fs->cell_height < 1.0f) fs->cell_height = 16.0f;

    (void)dpi;
    return true;
}

/* ─── font_init ──────────────────────────────────────────────────────────── */

bool font_init(FontState *fs, const wchar_t *family, float pt_size, float dpi) {
    memset(fs, 0, sizeof(*fs));

    wcsncpy(fs->family, family ? family : L"Cascadia Code", 127);
    fs->pt_size = pt_size > 0 ? pt_size : 13.0f;

    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     &IID_IDWriteFactory,
                                     (IUnknown **)&fs->factory);
    if (FAILED(hr) || !fs->factory) {
        wsh_log("DWriteCreateFactory failed: 0x%08X", hr);
        return false;
    }

    float px = fs->pt_size * dpi / 72.0f;

    fs->fmt_normal     = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL);
    fs->fmt_bold       = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_BOLD,   DWRITE_FONT_STYLE_NORMAL);
    fs->fmt_italic     = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC);
    fs->fmt_bold_italic= make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_BOLD,   DWRITE_FONT_STYLE_ITALIC);

    if (!fs->fmt_normal) { font_free(fs); return false; }

    return measure_cell(fs, dpi);
}

/* ─── font_resize ────────────────────────────────────────────────────────── */

bool font_resize(FontState *fs, float pt_size, float dpi) {
    if (!fs->factory) return false;
    if (fs->fmt_normal)     { fs->fmt_normal->lpVtbl->Release(fs->fmt_normal);          fs->fmt_normal = NULL; }
    if (fs->fmt_bold)       { fs->fmt_bold->lpVtbl->Release(fs->fmt_bold);              fs->fmt_bold = NULL; }
    if (fs->fmt_italic)     { fs->fmt_italic->lpVtbl->Release(fs->fmt_italic);          fs->fmt_italic = NULL; }
    if (fs->fmt_bold_italic){ fs->fmt_bold_italic->lpVtbl->Release(fs->fmt_bold_italic);fs->fmt_bold_italic = NULL; }

    fs->pt_size = pt_size;
    float px = pt_size * dpi / 72.0f;

    fs->fmt_normal     = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL);
    fs->fmt_bold       = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_BOLD,   DWRITE_FONT_STYLE_NORMAL);
    fs->fmt_italic     = make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC);
    fs->fmt_bold_italic= make_fmt(fs->factory, fs->family, px, DWRITE_FONT_WEIGHT_BOLD,   DWRITE_FONT_STYLE_ITALIC);

    return measure_cell(fs, dpi);
}

/* ─── font_free ──────────────────────────────────────────────────────────── */

void font_free(FontState *fs) {
    if (fs->fmt_bold_italic){ fs->fmt_bold_italic->lpVtbl->Release(fs->fmt_bold_italic); fs->fmt_bold_italic = NULL; }
    if (fs->fmt_italic)     { fs->fmt_italic->lpVtbl->Release(fs->fmt_italic);           fs->fmt_italic = NULL; }
    if (fs->fmt_bold)       { fs->fmt_bold->lpVtbl->Release(fs->fmt_bold);               fs->fmt_bold = NULL; }
    if (fs->fmt_normal)     { fs->fmt_normal->lpVtbl->Release(fs->fmt_normal);           fs->fmt_normal = NULL; }
    if (fs->factory)        { fs->factory->lpVtbl->Release(fs->factory);                  fs->factory = NULL; }
}
