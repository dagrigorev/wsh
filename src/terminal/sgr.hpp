/* Ported from Ghostty src/terminal/sgr.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * SGR (Select Graphic Rendition) attribute parsing: turning the parameters of
 * a CSI ... m sequence into styling attributes.
 *
 * The parameter meanings are ECMA-48 plus two widely-implemented extensions:
 * the 38/48/58 extended color forms, and the 4:N underline styles originating
 * with Kitty. Because those are published standards rather than invented
 * behavior, this file is written against the specifications and the tests
 * assert the specified values, instead of asserting whatever the port happens
 * to produce.
 *
 * Porting notes:
 *   - Zig's tagged union becomes a tag plus flat payload fields. A real C++
 *     union is not worth it here: an Attribute is transient, never stored per
 *     cell, and RGB has user-provided constructors which a union would then
 *     have to manage by hand.
 *   - Zig's `?Attribute` return becomes a bool plus an out parameter.
 *   - Unknown sequences report the parameter span rather than copying it, so
 *     the caller can decide whether to keep or log it.
 */

#pragma once
#ifndef WISP_TERMINAL_SGR_HPP
#define WISP_TERMINAL_SGR_HPP

#include <stddef.h>
#include <stdint.h>

#include "color.hpp"

namespace wisp {
namespace terminal {

/* ─── underline styles ───────────────────────────────────────────────────── */

/* Selected by the 4:N form. 4 on its own means single. */
enum class Underline : uint8_t {
    none   = 0,
    single = 1,
    dbl    = 2,   /* "double" is a keyword */
    curly  = 3,
    dotted = 4,
    dashed = 5,
};

/* ─── attributes ─────────────────────────────────────────────────────────── */

enum class AttributeTag : uint8_t {
    unset,                  /* SGR 0, or an empty parameter list */
    unknown,                /* unrecognized; see unknown_start/unknown_len */

    bold,
    reset_bold,             /* SGR 22, which also resets faint */
    italic,
    reset_italic,
    faint,

    underline,              /* payload: underline */
    underline_color,        /* payload: rgb */
    underline_color_256,    /* payload: idx */
    reset_underline_color,

    overline,
    reset_overline,
    blink,
    reset_blink,
    inverse,
    reset_inverse,
    invisible,
    reset_invisible,
    strikethrough,
    reset_strikethrough,

    direct_color_fg,        /* payload: rgb */
    direct_color_bg,        /* payload: rgb */

    fg_8,                   /* payload: idx, a color::Name 0-7 */
    bg_8,
    bright_fg_8,            /* payload: idx, a color::Name 8-15 */
    bright_bg_8,

    fg_256,                 /* payload: idx, a palette index */
    bg_256,

    reset_fg,
    reset_bg,
};

struct Attribute {
    AttributeTag tag;
    Underline    underline;
    RGB          rgb;
    uint8_t      idx;

    /* For `unknown`: the span of parameters that were not understood. */
    uint16_t unknown_start;
    uint16_t unknown_len;

    Attribute()
        : tag(AttributeTag::unset), underline(Underline::none), rgb(),
          idx(0), unknown_start(0), unknown_len(0) {}
};

/* ─── parser ─────────────────────────────────────────────────────────────── */

/* Parses one CSI ... m parameter list into successive attributes.
 *
 * `colon_set` marks, per parameter index, whether that parameter was separated
 * from the previous one by a colon rather than a semicolon. That distinction
 * matters: colons introduce a sub-parameter list, which is only meaningful
 * after 4, 38, 48 and 58. Callers that do not track separators may pass null,
 * which is treated as all-semicolons.
 */
struct SgrParser {
    const uint16_t *params;
    size_t          params_len;
    const uint8_t  *colon_set;   /* one byte per parameter, or null */
    size_t          idx;

    SgrParser()
        : params(nullptr), params_len(0), colon_set(nullptr), idx(0) {}

    SgrParser(const uint16_t *p, size_t n, const uint8_t *colons = nullptr)
        : params(p), params_len(n), colon_set(colons), idx(0) {}

    bool is_colon(size_t i) const {
        return colon_set != nullptr && i < params_len && colon_set[i] != 0;
    }

    /* Produce the next attribute. Returns false when the list is exhausted.
     *
     * An empty parameter list yields a single `unset`, because CSI m with no
     * parameters means SGR 0. */
    bool next(Attribute *out) {
        if (idx >= params_len) {
            /* One past the end yields unset for an empty list, then stops. */
            const bool empty_list = (idx == 0 && params_len == 0);
            idx++;
            if (empty_list) {
                *out = Attribute();
                out->tag = AttributeTag::unset;
                return true;
            }
            return false;
        }

        const size_t start = idx;
        const uint16_t p = params[idx];
        idx++;

        *out = Attribute();

        /* A colon may only introduce a sub-parameter list after these. */
        if (is_colon(start) && p != 4 && p != 38 && p != 48 && p != 58) {
            return emit_unknown(out, start);
        }

        switch (p) {
            case 0:  out->tag = AttributeTag::unset; return true;
            case 1:  out->tag = AttributeTag::bold; return true;
            case 2:  out->tag = AttributeTag::faint; return true;
            case 3:  out->tag = AttributeTag::italic; return true;

            case 4: {
                out->tag = AttributeTag::underline;
                /* 4:N selects a style; bare 4 is single. */
                if (idx < params_len && is_colon(idx)) {
                    const uint16_t style = params[idx];
                    idx++;
                    if (style > 5) return emit_unknown(out, start);
                    out->underline = (Underline)style;
                } else {
                    out->underline = Underline::single;
                }
                return true;
            }

            case 5:  out->tag = AttributeTag::blink; return true;
            /* 6 is "rapid blink"; treated as blink, as everyone does. */
            case 6:  out->tag = AttributeTag::blink; return true;
            case 7:  out->tag = AttributeTag::inverse; return true;
            case 8:  out->tag = AttributeTag::invisible; return true;
            case 9:  out->tag = AttributeTag::strikethrough; return true;

            /* 21 is double underline in practice. ECMA-48 assigns it "bold
             * off", but no terminal implements that meaning. */
            case 21:
                out->tag = AttributeTag::underline;
                out->underline = Underline::dbl;
                return true;

            case 22: out->tag = AttributeTag::reset_bold; return true;
            case 23: out->tag = AttributeTag::reset_italic; return true;

            case 24:
                out->tag = AttributeTag::underline;
                out->underline = Underline::none;
                return true;

            case 25: out->tag = AttributeTag::reset_blink; return true;
            case 27: out->tag = AttributeTag::reset_inverse; return true;
            case 28: out->tag = AttributeTag::reset_invisible; return true;
            case 29: out->tag = AttributeTag::reset_strikethrough; return true;

            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                out->tag = AttributeTag::fg_8;
                out->idx = (uint8_t)(p - 30);
                return true;

            case 38: return parse_extended(out, start, true);
            case 39: out->tag = AttributeTag::reset_fg; return true;

            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                out->tag = AttributeTag::bg_8;
                out->idx = (uint8_t)(p - 40);
                return true;

            case 48: return parse_extended(out, start, false);
            case 49: out->tag = AttributeTag::reset_bg; return true;

            case 53: out->tag = AttributeTag::overline; return true;
            case 55: out->tag = AttributeTag::reset_overline; return true;

            case 58: return parse_underline_color(out, start);
            case 59: out->tag = AttributeTag::reset_underline_color; return true;

            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                out->tag = AttributeTag::bright_fg_8;
                out->idx = (uint8_t)(p - 90 + 8);
                return true;

            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                out->tag = AttributeTag::bright_bg_8;
                out->idx = (uint8_t)(p - 100 + 8);
                return true;

            default:
                return emit_unknown(out, start);
        }
    }

private:
    bool emit_unknown(Attribute *out, size_t start) {
        /* Swallow any colon-joined sub-parameters so the caller does not see
         * them as separate attributes. */
        while (idx < params_len && is_colon(idx)) idx++;

        *out = Attribute();
        out->tag = AttributeTag::unknown;
        out->unknown_start = (uint16_t)start;
        out->unknown_len = (uint16_t)(idx - start);
        return true;
    }

    /* Read the value at `idx`, advancing past it. Returns false at end. */
    bool take(uint16_t *v) {
        if (idx >= params_len) return false;
        *v = params[idx];
        idx++;
        return true;
    }

    /* Abandon a malformed extended-color sequence, consuming the parameters
     * that belonged to it before reporting it unknown.
     *
     * This matters for more than tidiness. Leaving the tail unconsumed would
     * let it be reparsed as ordinary attributes, and a trailing 0 in, say,
     * "38;2;300;0;0" would then be read as SGR 0 and reset everything. A bad
     * color must not clear unrelated styling. */
    bool abandon_extended(Attribute *out, size_t start, size_t declared_end) {
        if (declared_end > params_len) declared_end = params_len;
        if (idx < declared_end) idx = declared_end;
        return emit_unknown(out, start);
    }

    /* 38/48: extended foreground or background color.
     *
     *   ...;5;N        palette index N
     *   ...;2;R;G;B    direct color
     *
     * The colon form additionally allows a colorspace slot that is ignored:
     *   ...:2::R:G:B
     */
    bool parse_extended(Attribute *out, size_t start, bool is_fg) {
        uint16_t kind;
        if (!take(&kind)) return emit_unknown(out, start);

        if (kind == 5) {
            const size_t end = start + 3;   /* 38, 5, N */
            uint16_t n;
            if (!take(&n) || n > 255) return abandon_extended(out, start, end);
            out->tag = is_fg ? AttributeTag::fg_256 : AttributeTag::bg_256;
            out->idx = (uint8_t)n;
            return true;
        }

        if (kind == 2) {
            /* In the colon form an empty colorspace slot may precede the
             * components, giving four values instead of three. */
            const size_t avail = params_len - idx;
            const bool colon_form = is_colon(start + 1);
            const bool has_colorspace = colon_form && avail >= 4;
            if (has_colorspace) idx++;

            /* The form spans the selector, the kind, three components, and
             * the optional colorspace slot. End is exclusive. */
            const size_t end = start + 5 + (has_colorspace ? 1 : 0);

            uint16_t c[3];
            for (int i = 0; i < 3; i++) {
                if (!take(&c[i]) || c[i] > 255) {
                    return abandon_extended(out, start, end);
                }
            }
            out->tag = is_fg ? AttributeTag::direct_color_fg
                             : AttributeTag::direct_color_bg;
            out->rgb = RGB((uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2]);
            return true;
        }

        return emit_unknown(out, start);
    }

    /* 58: underline color, same extended forms as 38/48. */
    bool parse_underline_color(Attribute *out, size_t start) {
        uint16_t kind;
        if (!take(&kind)) return emit_unknown(out, start);

        if (kind == 5) {
            const size_t end = start + 3;
            uint16_t n;
            if (!take(&n) || n > 255) return abandon_extended(out, start, end);
            out->tag = AttributeTag::underline_color_256;
            out->idx = (uint8_t)n;
            return true;
        }

        if (kind == 2) {
            const size_t avail = params_len - idx;
            const bool colon_form = is_colon(start + 1);
            const bool has_colorspace = colon_form && avail >= 4;
            if (has_colorspace) idx++;

            const size_t end = start + 5 + (has_colorspace ? 1 : 0);

            uint16_t c[3];
            for (int i = 0; i < 3; i++) {
                if (!take(&c[i]) || c[i] > 255) {
                    return abandon_extended(out, start, end);
                }
            }
            out->tag = AttributeTag::underline_color;
            out->rgb = RGB((uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2]);
            return true;
        }

        return emit_unknown(out, start);
    }
};

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_SGR_HPP */
