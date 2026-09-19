/* Tests for src/terminal/sgr.hpp.
 *
 * Related to Ghostty src/terminal/sgr.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Expectations here come from ECMA-48 and the documented 38/48/58 and 4:N
 * extensions, not from the port's own output.
 */

#include "test_helpers.h"
#include "sgr.hpp"

using namespace wisp::terminal;

/* Parse a parameter list and return the single attribute it should yield. */
static bool one(const uint16_t *p, size_t n, Attribute *out,
                const uint8_t *colons = nullptr) {
    SgrParser parser(p, n, colons);
    if (!parser.next(out)) return false;
    Attribute extra;
    /* Must be exactly one attribute. */
    return !parser.next(&extra);
}

/* ─── basics ─────────────────────────────────────────────────────────────── */

TEST(sgr, empty_list_is_unset) {
    /* CSI m with no parameters means SGR 0. */
    Attribute a;
    SgrParser parser(nullptr, 0, nullptr);
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::unset);
    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, zero_is_unset) {
    const uint16_t p[] = {0};
    Attribute a;
    ASSERT_TRUE(one(p, 1, &a));
    ASSERT_TRUE(a.tag == AttributeTag::unset);
}

TEST(sgr, simple_on_codes) {
    struct { uint16_t code; AttributeTag tag; } cases[] = {
        {1, AttributeTag::bold},
        {2, AttributeTag::faint},
        {3, AttributeTag::italic},
        {5, AttributeTag::blink},
        {7, AttributeTag::inverse},
        {8, AttributeTag::invisible},
        {9, AttributeTag::strikethrough},
        {53, AttributeTag::overline},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const uint16_t p[] = {cases[i].code};
        Attribute a;
        ASSERT_TRUE(one(p, 1, &a));
        ASSERT_TRUE(a.tag == cases[i].tag);
    }
}

TEST(sgr, reset_codes) {
    struct { uint16_t code; AttributeTag tag; } cases[] = {
        {22, AttributeTag::reset_bold},
        {23, AttributeTag::reset_italic},
        {25, AttributeTag::reset_blink},
        {27, AttributeTag::reset_inverse},
        {28, AttributeTag::reset_invisible},
        {29, AttributeTag::reset_strikethrough},
        {39, AttributeTag::reset_fg},
        {49, AttributeTag::reset_bg},
        {55, AttributeTag::reset_overline},
        {59, AttributeTag::reset_underline_color},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const uint16_t p[] = {cases[i].code};
        Attribute a;
        ASSERT_TRUE(one(p, 1, &a));
        ASSERT_TRUE(a.tag == cases[i].tag);
    }
}

/* ─── underline ──────────────────────────────────────────────────────────── */

TEST(sgr, bare_4_is_single_underline) {
    const uint16_t p[] = {4};
    Attribute a;
    ASSERT_TRUE(one(p, 1, &a));
    ASSERT_TRUE(a.tag == AttributeTag::underline);
    ASSERT_TRUE(a.underline == Underline::single);
}

TEST(sgr, 24_turns_underline_off) {
    /* Represented as underline with style none, not a separate reset tag. */
    const uint16_t p[] = {24};
    Attribute a;
    ASSERT_TRUE(one(p, 1, &a));
    ASSERT_TRUE(a.tag == AttributeTag::underline);
    ASSERT_TRUE(a.underline == Underline::none);
}

TEST(sgr, 21_is_double_underline) {
    /* ECMA-48 assigns 21 to "bold off", but every terminal treats it as
     * double underline, so that is what is implemented. */
    const uint16_t p[] = {21};
    Attribute a;
    ASSERT_TRUE(one(p, 1, &a));
    ASSERT_TRUE(a.tag == AttributeTag::underline);
    ASSERT_TRUE(a.underline == Underline::dbl);
}

TEST(sgr, colon_underline_styles) {
    const Underline want[] = {
        Underline::none, Underline::single, Underline::dbl,
        Underline::curly, Underline::dotted, Underline::dashed,
    };
    for (uint16_t style = 0; style <= 5; style++) {
        const uint16_t p[] = {4, style};
        const uint8_t colons[] = {0, 1};   /* the style is colon-joined */
        Attribute a;
        ASSERT_TRUE(one(p, 2, &a, colons));
        ASSERT_TRUE(a.tag == AttributeTag::underline);
        ASSERT_TRUE(a.underline == want[style]);
    }
}

TEST(sgr, colon_underline_out_of_range_is_unknown) {
    const uint16_t p[] = {4, 6};
    const uint8_t colons[] = {0, 1};
    Attribute a;
    ASSERT_TRUE(one(p, 2, &a, colons));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);
}

/* ─── 8-color ────────────────────────────────────────────────────────────── */

TEST(sgr, fg_and_bg_8_color) {
    for (uint16_t i = 0; i < 8; i++) {
        const uint16_t fg[] = {(uint16_t)(30 + i)};
        Attribute a;
        ASSERT_TRUE(one(fg, 1, &a));
        ASSERT_TRUE(a.tag == AttributeTag::fg_8);
        ASSERT_EQ(a.idx, i);

        const uint16_t bg[] = {(uint16_t)(40 + i)};
        Attribute b;
        ASSERT_TRUE(one(bg, 1, &b));
        ASSERT_TRUE(b.tag == AttributeTag::bg_8);
        ASSERT_EQ(b.idx, i);
    }
}

TEST(sgr, bright_fg_and_bg_map_to_palette_8_through_15) {
    for (uint16_t i = 0; i < 8; i++) {
        const uint16_t fg[] = {(uint16_t)(90 + i)};
        Attribute a;
        ASSERT_TRUE(one(fg, 1, &a));
        ASSERT_TRUE(a.tag == AttributeTag::bright_fg_8);
        ASSERT_EQ(a.idx, i + 8);

        const uint16_t bg[] = {(uint16_t)(100 + i)};
        Attribute b;
        ASSERT_TRUE(one(bg, 1, &b));
        ASSERT_TRUE(b.tag == AttributeTag::bright_bg_8);
        ASSERT_EQ(b.idx, i + 8);
    }
}

/* ─── extended color ─────────────────────────────────────────────────────── */

TEST(sgr, palette_256_fg_and_bg) {
    const uint16_t fg[] = {38, 5, 200};
    Attribute a;
    ASSERT_TRUE(one(fg, 3, &a));
    ASSERT_TRUE(a.tag == AttributeTag::fg_256);
    ASSERT_EQ(a.idx, 200);

    const uint16_t bg[] = {48, 5, 17};
    Attribute b;
    ASSERT_TRUE(one(bg, 3, &b));
    ASSERT_TRUE(b.tag == AttributeTag::bg_256);
    ASSERT_EQ(b.idx, 17);
}

TEST(sgr, direct_color_fg_and_bg) {
    const uint16_t fg[] = {38, 2, 10, 20, 30};
    Attribute a;
    ASSERT_TRUE(one(fg, 5, &a));
    ASSERT_TRUE(a.tag == AttributeTag::direct_color_fg);
    ASSERT_EQ(a.rgb.r, 10);
    ASSERT_EQ(a.rgb.g, 20);
    ASSERT_EQ(a.rgb.b, 30);

    const uint16_t bg[] = {48, 2, 255, 128, 0};
    Attribute b;
    ASSERT_TRUE(one(bg, 5, &b));
    ASSERT_TRUE(b.tag == AttributeTag::direct_color_bg);
    ASSERT_EQ(b.rgb.r, 255);
    ASSERT_EQ(b.rgb.g, 128);
    ASSERT_EQ(b.rgb.b, 0);
}

TEST(sgr, direct_color_colon_form_skips_colorspace) {
    /* 38:2::R:G:B — the empty slot after 2 is a colorspace id, ignored. */
    const uint16_t p[] = {38, 2, 0, 1, 2, 3};
    const uint8_t colons[] = {0, 1, 1, 1, 1, 1};
    Attribute a;
    ASSERT_TRUE(one(p, 6, &a, colons));
    ASSERT_TRUE(a.tag == AttributeTag::direct_color_fg);
    ASSERT_EQ(a.rgb.r, 1);
    ASSERT_EQ(a.rgb.g, 2);
    ASSERT_EQ(a.rgb.b, 3);
}

TEST(sgr, component_above_255_is_rejected) {
    const uint16_t p[] = {38, 2, 300, 0, 0};
    Attribute a;
    ASSERT_TRUE(one(p, 5, &a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);
}

TEST(sgr, malformed_color_does_not_leak_a_reset) {
    /* The tail of a rejected extended-color sequence must be consumed with
     * it. If it were reparsed, the trailing 0 here would be read as SGR 0 and
     * reset every attribute — a bad color must not clear unrelated styling. */
    const uint16_t p[] = {38, 2, 300, 0, 0};
    SgrParser parser(p, 5, nullptr);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);

    /* Nothing further, and in particular no unset. */
    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, malformed_color_still_allows_later_attributes) {
    /* Consuming the bad sequence must not swallow what legitimately follows. */
    const uint16_t p[] = {38, 2, 300, 0, 0, 1};
    SgrParser parser(p, 6, nullptr);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);

    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::bold);

    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, palette_index_above_255_is_rejected) {
    const uint16_t p[] = {38, 5, 256};
    Attribute a;
    ASSERT_TRUE(one(p, 3, &a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);
}

TEST(sgr, truncated_extended_color_is_unknown) {
    const uint16_t p[] = {38, 2, 10};   /* missing g and b */
    Attribute a;
    ASSERT_TRUE(one(p, 3, &a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);

    const uint16_t q[] = {38};          /* missing everything */
    Attribute b;
    ASSERT_TRUE(one(q, 1, &b));
    ASSERT_TRUE(b.tag == AttributeTag::unknown);
}

TEST(sgr, underline_color_58) {
    const uint16_t pal[] = {58, 5, 42};
    Attribute a;
    ASSERT_TRUE(one(pal, 3, &a));
    ASSERT_TRUE(a.tag == AttributeTag::underline_color_256);
    ASSERT_EQ(a.idx, 42);

    const uint16_t rgb[] = {58, 2, 1, 2, 3};
    Attribute b;
    ASSERT_TRUE(one(rgb, 5, &b));
    ASSERT_TRUE(b.tag == AttributeTag::underline_color);
    ASSERT_EQ(b.rgb.r, 1);
    ASSERT_EQ(b.rgb.b, 3);
}

/* ─── sequences ──────────────────────────────────────────────────────────── */

TEST(sgr, multiple_attributes_in_one_sequence) {
    /* CSI 1;4;31 m — bold, underline, red foreground. */
    const uint16_t p[] = {1, 4, 31};
    SgrParser parser(p, 3, nullptr);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::bold);

    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::underline);
    ASSERT_TRUE(a.underline == Underline::single);

    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::fg_8);
    ASSERT_EQ(a.idx, 1);

    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, extended_color_then_more_attributes) {
    /* The extended form must consume exactly its own parameters. */
    const uint16_t p[] = {38, 5, 9, 1};
    SgrParser parser(p, 4, nullptr);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::fg_256);
    ASSERT_EQ(a.idx, 9);

    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::bold);

    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, unknown_code_reports_span_and_continues) {
    const uint16_t p[] = {1, 12345, 1};
    SgrParser parser(p, 3, nullptr);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::bold);

    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);
    ASSERT_EQ(a.unknown_start, 1);
    ASSERT_EQ(a.unknown_len, 1);

    /* Parsing recovers rather than abandoning the rest. */
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::bold);

    ASSERT_FALSE(parser.next(&a));
}

TEST(sgr, stray_colon_parameter_is_unknown) {
    /* A colon is only meaningful after 4, 38, 48 or 58. */
    const uint16_t p[] = {1, 2};
    const uint8_t colons[] = {1, 0};
    SgrParser parser(p, 2, colons);

    Attribute a;
    ASSERT_TRUE(parser.next(&a));
    ASSERT_TRUE(a.tag == AttributeTag::unknown);
}
