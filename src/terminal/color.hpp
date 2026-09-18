/* Ported from Ghostty src/terminal/color.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Terminal color types: RGB, the 16 named ANSI colors, and the 256-color
 * palette.
 *
 * PARTIAL PORT. Ported here:
 *
 *   - RGB, with equality, WCAG relative luminance and contrast ratio
 *   - Name, the 16 named ANSI colors, and their default values
 *   - Palette and the default 256-color palette
 *   - hex color parsing
 *
 * Deliberately not ported yet, because nothing in the page/style cluster
 * needs them and each drags in further dependencies:
 *
 *   - generate256Color, which derives a palette from a base16 theme by
 *     trilinear interpolation in CIELAB. Needs LAB and fraction.zig.
 *   - DynamicPalette and DynamicRGB, which are allocator-backed.
 *   - Special and Dynamic, the OSC 4/5 color selectors.
 *   - RGB.parse's X11 named-color path, which lives in x11_color.zig — the
 *     other half of a cycle with this file (see docs/GHOSTTY_PORT_ORDER.md).
 *
 * The ledger records this file as `wip`, not `done`.
 */

#pragma once
#ifndef WISP_TERMINAL_COLOR_HPP
#define WISP_TERMINAL_COLOR_HPP

#include <stddef.h>
#include <stdint.h>
#include <math.h>

namespace wisp {
namespace terminal {

/* ─── RGB ────────────────────────────────────────────────────────────────── */

struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    /* Explicit constructors rather than default member initializers: an
     * aggregate with NSDMIs is only brace-initializable from C++14 on, and
     * the palette tables below rely on brace init. */
    RGB() : r(0), g(0), b(0) {}
    RGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

    bool eql(const RGB &o) const { return r == o.r && g == o.g && b == o.b; }

    /* WCAG 2.x relative luminance: linearize each channel out of sRGB's
     * transfer function, then weight by the CIE luminous efficiency of each
     * primary. Used for contrast, so it must be the perceptual quantity, not
     * a naive channel average. */
    double luminance() const {
        return 0.2126 * linearize(r) + 0.7152 * linearize(g) + 0.0722 * linearize(b);
    }

    /* WCAG contrast ratio, always >= 1. The +0.05 keeps the ratio finite when
     * one color is pure black. */
    double contrast(const RGB &o) const {
        const double a = luminance();
        const double c = o.luminance();
        const double hi = a > c ? a : c;
        const double lo = a > c ? c : a;
        return (hi + 0.05) / (lo + 0.05);
    }

    /* Perceived lightness, i.e. CIELAB L* from relative luminance. Unlike
     * luminance() this is roughly linear in how light a color looks, which is
     * what you want when picking a readable foreground. Range 0..100. */
    double perceived_luminance() const {
        const double y = luminance();
        if (y <= 216.0 / 24389.0) return y * (24389.0 / 27.0);
        return pow(y, 1.0 / 3.0) * 116.0 - 16.0;
    }

private:
    static double linearize(uint8_t v) {
        const double c = (double)v / 255.0;
        if (c <= 0.04045) return c / 12.92;
        return pow((c + 0.055) / 1.055, 2.4);
    }
};

/* ─── Name ───────────────────────────────────────────────────────────────── */

/* The 16 named ANSI colors. Values above 15 are valid unnamed entries in the
 * 256-color palette, which is why this is a plain uint8_t rather than a closed
 * enumeration. */
enum Name : uint8_t {
    COLOR_BLACK = 0,
    COLOR_RED = 1,
    COLOR_GREEN = 2,
    COLOR_YELLOW = 3,
    COLOR_BLUE = 4,
    COLOR_MAGENTA = 5,
    COLOR_CYAN = 6,
    COLOR_WHITE = 7,

    COLOR_BRIGHT_BLACK = 8,
    COLOR_BRIGHT_RED = 9,
    COLOR_BRIGHT_GREEN = 10,
    COLOR_BRIGHT_YELLOW = 11,
    COLOR_BRIGHT_BLUE = 12,
    COLOR_BRIGHT_MAGENTA = 13,
    COLOR_BRIGHT_CYAN = 14,
    COLOR_BRIGHT_WHITE = 15,
};

/* Default value for a named color. Returns false for indices above 15, which
 * have no name and therefore no default. */
inline bool name_default(uint8_t idx, RGB *out) {
    static const RGB defaults[16] = {
        {0x1D, 0x1F, 0x21}, {0xCC, 0x66, 0x66}, {0xB5, 0xBD, 0x68}, {0xF0, 0xC6, 0x74},
        {0x81, 0xA2, 0xBE}, {0xB2, 0x94, 0xBB}, {0x8A, 0xBE, 0xB7}, {0xC5, 0xC8, 0xC6},
        {0x66, 0x66, 0x66}, {0xD5, 0x4E, 0x53}, {0xB9, 0xCA, 0x4A}, {0xE7, 0xC5, 0x47},
        {0x7A, 0xA6, 0xDA}, {0xC3, 0x97, 0xD8}, {0x70, 0xC0, 0xB1}, {0xEA, 0xEA, 0xEA},
    };
    if (idx >= 16) return false;
    *out = defaults[idx];
    return true;
}

/* ─── Palette ────────────────────────────────────────────────────────────── */

static const size_t PALETTE_SIZE = 256;

struct Palette {
    RGB colors[PALETTE_SIZE];

    RGB &operator[](size_t i) { return colors[i]; }
    const RGB &operator[](size_t i) const { return colors[i]; }
};

/* The default 256-color palette.
 *
 * Entries 0-15 are the named colors. 16-231 are a 6x6x6 RGB cube and 232-255
 * a 24-step gray ramp, both built with the standard xterm formulas: cube
 * levels are 0 then 55 + 40n, and gray steps are 8 + 10n. Those two ranges
 * are a fixed convention rather than a choice, so they are computed here and
 * checked against the convention in the tests. */
inline Palette default_palette() {
    Palette p;
    size_t i = 0;

    for (; i < 16; i++) name_default((uint8_t)i, &p.colors[i]);

    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                p.colors[i].r = (uint8_t)(r == 0 ? 0 : r * 40 + 55);
                p.colors[i].g = (uint8_t)(g == 0 ? 0 : g * 40 + 55);
                p.colors[i].b = (uint8_t)(b == 0 ? 0 : b * 40 + 55);
                i++;
            }
        }
    }

    for (; i < PALETTE_SIZE; i++) {
        const uint8_t v = (uint8_t)(((i - 232) * 10) + 8);
        p.colors[i].r = v;
        p.colors[i].g = v;
        p.colors[i].b = v;
    }

    return p;
}

/* ─── parsing ────────────────────────────────────────────────────────────── */

/* Parse a hex color: "#RGB", "#RRGGBB", or either without the leading '#'.
 * The short form expands each nibble by duplication, so "#abc" is "#aabbcc".
 *
 * The X11 named-color form that upstream also accepts is not handled here;
 * see the header comment. */
inline bool parse_hex_color(const char *s, size_t len, RGB *out) {
    if (!s) return false;
    if (len > 0 && s[0] == '#') { s++; len--; }
    if (len != 3 && len != 6) return false;

    int v[6];
    for (size_t i = 0; i < len; i++) {
        const char c = s[i];
        if (c >= '0' && c <= '9')      v[i] = c - '0';
        else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
        else return false;
    }

    if (len == 3) {
        out->r = (uint8_t)(v[0] * 16 + v[0]);
        out->g = (uint8_t)(v[1] * 16 + v[1]);
        out->b = (uint8_t)(v[2] * 16 + v[2]);
    } else {
        out->r = (uint8_t)(v[0] * 16 + v[1]);
        out->g = (uint8_t)(v[2] * 16 + v[3]);
        out->b = (uint8_t)(v[4] * 16 + v[5]);
    }
    return true;
}

/* A parsed "N=COLOR" palette override. */
struct PaletteEntry {
    uint8_t index;
    RGB     color;
};

/* Parse "N=COLOR", where N is a decimal palette index 0-255 and COLOR is
 * anything parse_hex_color accepts. Surrounding spaces and tabs are ignored.
 *
 * Upstream also accepts 0x/0o/0b prefixes on N; only decimal is handled here,
 * and a value above 255 is rejected rather than wrapping. */
inline bool parse_palette_entry(const char *s, size_t len, PaletteEntry *out) {
    if (!s) return false;

    size_t eq = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '=') { eq = i; break; }
    }
    if (eq == (size_t)-1) return false;

    size_t a = 0, b = eq;
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    if (a == b) return false;

    unsigned idx = 0;
    for (size_t i = a; i < b; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        idx = idx * 10 + (unsigned)(s[i] - '0');
        if (idx > 255) return false;
    }

    size_t c = eq + 1, d = len;
    while (c < d && (s[c] == ' ' || s[c] == '\t')) c++;
    while (d > c && (s[d - 1] == ' ' || s[d - 1] == '\t')) d--;

    RGB rgb;
    if (!parse_hex_color(s + c, d - c, &rgb)) return false;

    out->index = (uint8_t)idx;
    out->color = rgb;
    return true;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_COLOR_HPP */
