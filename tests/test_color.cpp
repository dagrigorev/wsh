/* Tests for src/terminal/color.hpp.
 *
 * Related to Ghostty src/terminal/color.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * These deliberately check the palette against the xterm convention and the
 * WCAG formulas rather than against the values the port happens to produce.
 * A transcription slip should fail here; checking output against itself would
 * not catch one.
 */

#include "test_helpers.h"
#include "color.hpp"

using namespace wisp::terminal;

/* ─── palette structure ──────────────────────────────────────────────────── */

TEST(color, palette_cube_levels_follow_xterm) {
    Palette p = default_palette();

    /* The 6x6x6 cube uses levels 0, 95, 135, 175, 215, 255 — that is 0 for
     * the first step and 55 + 40n thereafter. This is fixed convention, not
     * a choice, so it is safe to assert literally. */
    const uint8_t expect[6] = { 0, 95, 135, 175, 215, 255 };

    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                const size_t idx = 16 + (size_t)(r * 36 + g * 6 + b);
                ASSERT_EQ(p[idx].r, expect[r]);
                ASSERT_EQ(p[idx].g, expect[g]);
                ASSERT_EQ(p[idx].b, expect[b]);
            }
        }
    }
}

TEST(color, palette_cube_endpoints) {
    Palette p = default_palette();

    /* The cube's corners are pure black and pure white. */
    ASSERT_EQ(p[16].r, 0);
    ASSERT_EQ(p[16].g, 0);
    ASSERT_EQ(p[16].b, 0);

    ASSERT_EQ(p[231].r, 255);
    ASSERT_EQ(p[231].g, 255);
    ASSERT_EQ(p[231].b, 255);
}

TEST(color, palette_gray_ramp) {
    Palette p = default_palette();

    /* 24 gray steps at 8 + 10n, so 8 through 238, and always neutral. */
    for (size_t i = 232; i < 256; i++) {
        const uint8_t want = (uint8_t)(8 + (i - 232) * 10);
        ASSERT_EQ(p[i].r, want);
        ASSERT_EQ(p[i].g, want);
        ASSERT_EQ(p[i].b, want);
    }

    ASSERT_EQ(p[232].r, 8);
    ASSERT_EQ(p[255].r, 238);
}

TEST(color, palette_named_entries) {
    Palette p = default_palette();

    /* The first 16 entries are the named colors, in order. */
    for (uint8_t i = 0; i < 16; i++) {
        RGB named;
        ASSERT_TRUE(name_default(i, &named));
        ASSERT_TRUE(p[i].eql(named));
    }
}

TEST(color, name_default_rejects_unnamed) {
    RGB out;
    ASSERT_TRUE(name_default(0, &out));
    ASSERT_TRUE(name_default(15, &out));

    /* Above 15 there is no name and therefore no default. */
    ASSERT_FALSE(name_default(16, &out));
    ASSERT_FALSE(name_default(255, &out));
}

TEST(color, bright_variants_are_distinct) {
    /* Every bright color differs from its base. This catches a duplicated or
     * misaligned table, which is what a transcription slip looks like.
     *
     * Note it does NOT assert that bright is lighter: in this theme red,
     * yellow and cyan are perceptually *darker* than their base, because
     * "bright" here means more saturated rather than lighter. An earlier
     * version of this test asserted lightness and failed on exactly those
     * three. */
    for (uint8_t i = 0; i < 8; i++) {
        RGB base, bright;
        ASSERT_TRUE(name_default(i, &base));
        ASSERT_TRUE(name_default((uint8_t)(i + 8), &bright));
        ASSERT_FALSE(base.eql(bright));
    }
}

TEST(color, black_and_white_brights_are_lighter) {
    /* The two achromatic pairs are the ones where lightness genuinely does
     * increase, so those are safe to assert. */
    RGB black, bright_black, white, bright_white;
    ASSERT_TRUE(name_default(COLOR_BLACK, &black));
    ASSERT_TRUE(name_default(COLOR_BRIGHT_BLACK, &bright_black));
    ASSERT_TRUE(name_default(COLOR_WHITE, &white));
    ASSERT_TRUE(name_default(COLOR_BRIGHT_WHITE, &bright_white));

    ASSERT_TRUE(bright_black.perceived_luminance() > black.perceived_luminance());
    ASSERT_TRUE(bright_white.perceived_luminance() > white.perceived_luminance());
}

/* ─── luminance and contrast ─────────────────────────────────────────────── */

TEST(color, contrast_black_on_white_is_21) {
    /* WCAG's maximum contrast ratio is exactly 21:1 for pure black against
     * pure white. Hitting that value is a strong check that the sRGB
     * linearization and the luminance weights are both right. */
    RGB black(0, 0, 0);
    RGB white(255, 255, 255);

    const double c = black.contrast(white);
    ASSERT_TRUE(c > 20.99 && c < 21.01);

    /* Contrast is symmetric. */
    const double c2 = white.contrast(black);
    ASSERT_TRUE(c2 > 20.99 && c2 < 21.01);
}

TEST(color, contrast_with_self_is_1) {
    RGB c(0x81, 0xA2, 0xBE);
    const double v = c.contrast(c);
    ASSERT_TRUE(v > 0.999 && v < 1.001);
}

TEST(color, luminance_endpoints) {
    ASSERT_TRUE(RGB(0, 0, 0).luminance() < 0.0001);

    const double w = RGB(255, 255, 255).luminance();
    ASSERT_TRUE(w > 0.9999 && w < 1.0001);
}

TEST(color, luminance_weights_green_highest) {
    /* The CIE luminous efficiency weights are green > red > blue, so a
     * saturated primary at full intensity must order that way. Swapped
     * coefficients would flip this. */
    const double r = RGB(255, 0, 0).luminance();
    const double g = RGB(0, 255, 0).luminance();
    const double b = RGB(0, 0, 255).luminance();

    ASSERT_TRUE(g > r);
    ASSERT_TRUE(r > b);
}

TEST(color, perceived_luminance_endpoints) {
    /* CIELAB L* runs 0..100. */
    const double black = RGB(0, 0, 0).perceived_luminance();
    ASSERT_TRUE(black > -0.001 && black < 0.001);

    const double white = RGB(255, 255, 255).perceived_luminance();
    ASSERT_TRUE(white > 99.99 && white < 100.01);

    /* Mid gray sits near 50 perceptually, which is the whole point of L* —
     * its relative luminance is only about 0.21. */
    const double mid = RGB(128, 128, 128).perceived_luminance();
    ASSERT_TRUE(mid > 50.0 && mid < 55.0);
}

/* ─── parsing ────────────────────────────────────────────────────────────── */

TEST(color, parse_hex_long_form) {
    RGB c;
    ASSERT_TRUE(parse_hex_color("#1D1F21", 7, &c));
    ASSERT_EQ(c.r, 0x1D);
    ASSERT_EQ(c.g, 0x1F);
    ASSERT_EQ(c.b, 0x21);

    /* The leading '#' is optional. */
    ASSERT_TRUE(parse_hex_color("CC6666", 6, &c));
    ASSERT_EQ(c.r, 0xCC);
    ASSERT_EQ(c.g, 0x66);
    ASSERT_EQ(c.b, 0x66);
}

TEST(color, parse_hex_short_form_duplicates_nibbles) {
    RGB c;
    ASSERT_TRUE(parse_hex_color("#abc", 4, &c));
    ASSERT_EQ(c.r, 0xAA);
    ASSERT_EQ(c.g, 0xBB);
    ASSERT_EQ(c.b, 0xCC);
}

TEST(color, parse_hex_case_insensitive) {
    RGB lower, upper;
    ASSERT_TRUE(parse_hex_color("#abcdef", 7, &lower));
    ASSERT_TRUE(parse_hex_color("#ABCDEF", 7, &upper));
    ASSERT_TRUE(lower.eql(upper));
}

TEST(color, parse_hex_rejects_bad_input) {
    RGB c;
    ASSERT_FALSE(parse_hex_color("#12345", 6, &c));   /* wrong length */
    ASSERT_FALSE(parse_hex_color("#gggggg", 7, &c));  /* not hex */
    ASSERT_FALSE(parse_hex_color("", 0, &c));
    ASSERT_FALSE(parse_hex_color("#", 1, &c));
}

TEST(color, parse_palette_entry_basic) {
    PaletteEntry e;
    ASSERT_TRUE(parse_palette_entry("0=#1D1F21", 9, &e));
    ASSERT_EQ(e.index, 0);
    ASSERT_EQ(e.color.r, 0x1D);

    ASSERT_TRUE(parse_palette_entry("255=#ffffff", 11, &e));
    ASSERT_EQ(e.index, 255);
    ASSERT_EQ(e.color.b, 255);
}

TEST(color, parse_palette_entry_trims_whitespace) {
    PaletteEntry e;
    const char *s = "  12  =  #abc  ";
    ASSERT_TRUE(parse_palette_entry(s, strlen(s), &e));
    ASSERT_EQ(e.index, 12);
    ASSERT_EQ(e.color.r, 0xAA);
}

TEST(color, parse_palette_entry_rejects_bad_input) {
    PaletteEntry e;
    ASSERT_FALSE(parse_palette_entry("1D1F21", 6, &e));      /* no '=' */
    ASSERT_FALSE(parse_palette_entry("=#abc", 5, &e));       /* no index */
    ASSERT_FALSE(parse_palette_entry("256=#abc", 8, &e));    /* out of range */
    ASSERT_FALSE(parse_palette_entry("999=#abc", 8, &e));
    ASSERT_FALSE(parse_palette_entry("1=notacolor", 11, &e));
    ASSERT_FALSE(parse_palette_entry("x=#abc", 6, &e));      /* non-numeric */
}
