/* Tests for src/terminal/screen.hpp.
 *
 * Related to Ghostty src/terminal/Screen.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * A screen is a page list plus a cursor, and the cursor is two things at once:
 * a column and row on the screen, which is what escape sequences talk about,
 * and a pin into the text, which is what survives the pages changing. These
 * tests are mostly about those two staying in agreement.
 *
 * Named test_screen_port to keep it apart from test_screen.c, which covers
 * the live terminal buffer this does not yet replace.
 */

#include "test_helpers.h"
#include "screen.hpp"

using namespace wisp::terminal;

/* ─── setting up ─────────────────────────────────────────────────────────── */

TEST(screen, starts_at_the_top_left) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    ASSERT_EQ(s.cursor.x, 0);
    ASSERT_EQ(s.cursor.y, 0);
    ASSERT_TRUE(s.cursor.page_pin.pin.valid());
    ASSERT_TRUE(s.cursor.page_cell != nullptr);
    ASSERT_TRUE(s.cursor.page_row != nullptr);

    screen_deinit(&s);
}

TEST(screen, the_cursor_is_tracked_from_the_start) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    /* A cursor that was not tracked would be wrong after the first resize,
     * which is not a state worth having even briefly. */
    ASSERT_TRUE(s.pages.tracked == &s.cursor.page_pin);

    screen_deinit(&s);
    ASSERT_TRUE(s.cursor.page_cell == nullptr);
}

TEST(screen, the_cached_cell_is_the_cell_at_the_cursor) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    ASSERT_TRUE(screen_cursor_absolute(&s, 7, 3));
    s.cursor.page_cell->set_codepoint('q');

    /* The cached pointer is a shortcut, not a second copy. */
    ASSERT_EQ(screen_cell(&s, 7, 3)->codepoint(), 'q');

    screen_deinit(&s);
}

/* ─── moving ─────────────────────────────────────────────────────────────── */

TEST(screen, absolute_movement) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    ASSERT_TRUE(screen_cursor_absolute(&s, 12, 6));
    ASSERT_EQ(s.cursor.x, 12);
    ASSERT_EQ(s.cursor.y, 6);
    ASSERT_EQ(s.cursor.page_pin.pin.x, 12);

    screen_deinit(&s);
}

TEST(screen, movement_off_the_screen_is_refused) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 5, 5));

    ASSERT_FALSE(screen_cursor_absolute(&s, 40, 5));
    ASSERT_FALSE(screen_cursor_absolute(&s, 5, 10));

    /* Refused means unmoved. */
    ASSERT_EQ(s.cursor.x, 5);
    ASSERT_EQ(s.cursor.y, 5);

    screen_deinit(&s);
}

TEST(screen, relative_movement_clamps_at_the_edges) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    screen_cursor_left(&s, 5);
    ASSERT_EQ(s.cursor.x, 0);
    screen_cursor_up(&s, 5);
    ASSERT_EQ(s.cursor.y, 0);

    screen_cursor_right(&s, 1000);
    ASSERT_EQ(s.cursor.x, 39);
    screen_cursor_down(&s, 1000);
    ASSERT_EQ(s.cursor.y, 9);

    screen_deinit(&s);
}

TEST(screen, relative_movement_moves) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    screen_cursor_right(&s, 4);
    screen_cursor_down(&s, 3);
    ASSERT_EQ(s.cursor.x, 4);
    ASSERT_EQ(s.cursor.y, 3);

    screen_cursor_left(&s, 1);
    screen_cursor_up(&s, 2);
    ASSERT_EQ(s.cursor.x, 3);
    ASSERT_EQ(s.cursor.y, 1);

    screen_deinit(&s);
}

TEST(screen, the_pin_follows_the_coordinates) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    const size_t top = page_list_row_index(&s.pages,
                                           page_list_active_start(&s.pages));

    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 4));
    ASSERT_EQ(page_list_row_index(&s.pages, s.cursor.page_pin.pin), top + 4);

    screen_deinit(&s);
}

/* ─── scrolling ──────────────────────────────────────────────────────────── */

TEST(screen, moving_down_inside_the_screen_does_not_scroll) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));
    const size_t before = s.pages.row_count;

    ASSERT_TRUE(screen_cursor_down_scroll(&s));
    ASSERT_EQ(s.cursor.y, 1);
    ASSERT_EQ(s.pages.row_count, before);

    screen_deinit(&s);
}

TEST(screen, moving_down_at_the_bottom_scrolls) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 2, 9));
    const size_t before = s.pages.row_count;

    ASSERT_TRUE(screen_cursor_down_scroll(&s));

    /* The cursor stays on the last row; the screen moved under it. */
    ASSERT_EQ(s.cursor.y, 9);
    ASSERT_EQ(s.cursor.x, 2);
    ASSERT_EQ(s.pages.row_count, before + 1);
    ASSERT_EQ(page_list_max_scroll(&s.pages), 1u);

    screen_deinit(&s);
}

TEST(screen, what_scrolls_off_is_still_there) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 3, 0));

    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 0));
    s.cursor.page_cell->set_codepoint('t');
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 2));

    ASSERT_TRUE(screen_cursor_down_scroll(&s));

    /* The row that left the top of the screen was not moved anywhere. It is
     * the same row, in the same page — it is simply not on the screen. */
    ASSERT_EQ(screen_cell(&s, 0, 0)->codepoint(), 0u);
    Pin scrolled = page_list_pin(&s.pages, 0);
    scrolled.x = 0;
    ASSERT_EQ(scrolled.cell()->codepoint(), 't');

    screen_deinit(&s);
}

TEST(screen, scrolling_leaves_the_cached_pointers_usable) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 3, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 1, 2));

    /* Growth can allocate a page, and the cached pointers are addresses
     * inside one. Writing through a stale pointer here would land in the row
     * that used to be at the bottom. */
    ASSERT_TRUE(screen_cursor_down_scroll(&s));
    s.cursor.page_cell->set_codepoint('n');

    ASSERT_EQ(screen_cell(&s, 1, 2)->codepoint(), 'n');

    screen_deinit(&s);
}

TEST(screen, scrolling_many_times_keeps_the_cursor_at_the_bottom) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 3));

    for (int i = 0; i < 500; i++) {
        ASSERT_TRUE(screen_cursor_down_scroll(&s));
        s.cursor.page_cell->set_codepoint('x');
        ASSERT_EQ(s.cursor.y, 3);
    }

    ASSERT_EQ(s.pages.row_count, 504u);
    ASSERT_EQ(screen_cell(&s, 0, 3)->codepoint(), 'x');

    screen_deinit(&s);
}

/* ─── resizing ───────────────────────────────────────────────────────────── */

TEST(screen, resizing_the_height_keeps_the_cursor_on_its_row) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 3, 4));
    s.cursor.page_cell->set_codepoint('h');

    ASSERT_TRUE(screen_resize(&s, 40, 20));

    /* Nothing was re-laid, so the cursor is on the same character. Its row
     * number changed because the screen got taller underneath it. */
    ASSERT_EQ(s.cursor.page_cell->codepoint(), 'h');
    ASSERT_EQ(s.cursor.x, 3);

    screen_deinit(&s);
}

TEST(screen, resizing_the_width_keeps_the_cursor_on_its_character) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 4, 0));

    /* A line long enough that narrowing re-lays it. */
    for (CellCountInt x = 0; x < 20; x++) {
        ASSERT_TRUE(screen_cursor_absolute(&s, x, 0));
        s.cursor.page_cell->set_codepoint((uint32_t)('a' + x));
    }
    s.pages.first->page.get_row(s.cursor.page_pin.pin.y)->set_wrap(false);

    ASSERT_TRUE(screen_cursor_absolute(&s, 13, 0));
    ASSERT_EQ(s.cursor.page_cell->codepoint(), 'n');

    ASSERT_TRUE(screen_resize(&s, 5, 4));

    /* The cursor is a tracked pin, so the reflow moved it; the screen
     * coordinates were worked out again from where it landed. */
    ASSERT_EQ(s.cursor.page_cell->codepoint(), 'n');
    ASSERT_EQ(s.cursor.x, 3);

    screen_deinit(&s);
}

TEST(screen, the_cursor_stays_on_the_screen_after_a_resize) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 60, 4, 0));

    for (int i = 0; i < 30; i++) {
        ASSERT_TRUE(screen_cursor_absolute(&s, 0, 3));
        for (CellCountInt x = 0; x < 60; x++) {
            ASSERT_TRUE(screen_cursor_absolute(&s, x, 3));
            s.cursor.page_cell->set_codepoint('a');
        }
        ASSERT_TRUE(screen_cursor_down_scroll(&s));
    }

    ASSERT_TRUE(screen_cursor_absolute(&s, 10, 0));
    ASSERT_TRUE(screen_resize(&s, 6, 4));

    /* A cursor near the top of a screen that just got much narrower reflows
     * into the scrollback. A terminal cannot have a cursor off its screen,
     * so it is clamped onto it. */
    ASSERT_TRUE(s.cursor.y < s.pages.rows);
    ASSERT_TRUE(s.cursor.x < s.pages.cols);
    ASSERT_TRUE(s.cursor.page_cell != nullptr);

    screen_deinit(&s);
}

TEST(screen, the_cursor_is_still_writable_after_a_resize) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 5, 2));

    ASSERT_TRUE(screen_resize(&s, 9, 6));

    /* The list was rebuilt, so every page the cursor knew about is freed.
     * Writing through the cached pointer is the test. */
    s.cursor.page_cell->set_codepoint('w');
    ASSERT_EQ(screen_cell(&s, s.cursor.x, s.cursor.y)->codepoint(), 'w');

    screen_deinit(&s);
}

TEST(screen, a_refused_resize_leaves_the_cursor_alone) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 5, 2));

    ASSERT_FALSE(screen_resize(&s, 0, 4));
    ASSERT_EQ(s.cursor.x, 5);
    ASSERT_EQ(s.cursor.y, 2);
    ASSERT_TRUE(s.cursor.page_cell != nullptr);

    screen_deinit(&s);
}

TEST(screen, resizing_repeatedly_keeps_everything_consistent) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 6, 0));
    for (int i = 0; i < 20; i++) {
        ASSERT_TRUE(screen_cursor_down_scroll(&s));
    }

    const CellCountInt widths[] = {7, 13, 40, 5, 22, 40};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        ASSERT_TRUE(screen_resize(&s, widths[i], 6));

        ASSERT_TRUE(s.cursor.x < s.pages.cols);
        ASSERT_TRUE(s.cursor.y < s.pages.rows);
        ASSERT_TRUE(s.cursor.page_cell != nullptr);

        /* The cursor's two descriptions of itself still agree. */
        Pin from_coords = page_list_active_pin(&s.pages, s.cursor.x, s.cursor.y);
        ASSERT_TRUE(from_coords.valid());
        ASSERT_TRUE(from_coords.node == s.cursor.page_pin.pin.node);
        ASSERT_EQ(from_coords.y, s.cursor.page_pin.pin.y);

        s.cursor.page_cell->set_codepoint('k');
    }

    screen_deinit(&s);
}

/* ─── reading ────────────────────────────────────────────────────────────── */

TEST(screen, reading_off_the_screen_gives_nothing) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 10, 0));

    ASSERT_TRUE(screen_cell(&s, 39, 9) != nullptr);
    ASSERT_TRUE(screen_cell(&s, 40, 9) == nullptr);
    ASSERT_TRUE(screen_cell(&s, 39, 10) == nullptr);
    ASSERT_TRUE(screen_row(&s, 9) != nullptr);
    ASSERT_TRUE(screen_row(&s, 10) == nullptr);

    screen_deinit(&s);
}

TEST(screen, the_screen_is_the_bottom_of_the_list) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 40, 3, 0));

    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(screen_cursor_absolute(&s, 0, s.pages.rows - 1));
        s.cursor.page_cell->set_codepoint((uint32_t)('0' + i));
        ASSERT_TRUE(screen_cursor_down_scroll(&s));
    }

    /* Five lines written, three rows of screen: the last two plus the blank
     * row the cursor is on. */
    ASSERT_EQ(screen_cell(&s, 0, 0)->codepoint(), '3');
    ASSERT_EQ(screen_cell(&s, 0, 1)->codepoint(), '4');
    ASSERT_EQ(screen_cell(&s, 0, 2)->codepoint(), 0u);

    screen_deinit(&s);
}

/* ─── blank rows below the cursor ────────────────────────────────────────── */

TEST(screen, blank_rows_below_the_cursor_are_not_text) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 4, 0));

    /* A full line at the top, cursor on it, three blank rows below — which is
     * what a screen almost always looks like. Narrowing turns that one line
     * into four rows, and if the blanks below kept the room they used to, the
     * cursor would be pushed off the top of the screen. */
    for (CellCountInt x = 0; x < 20; x++) {
        ASSERT_TRUE(screen_cursor_absolute(&s, x, 0));
        s.cursor.page_cell->set_codepoint((uint32_t)('a' + x));
    }
    ASSERT_TRUE(screen_cursor_absolute(&s, 13, 0));

    ASSERT_TRUE(screen_resize(&s, 5, 4));

    ASSERT_EQ(s.cursor.page_cell->codepoint(), 'n');
    ASSERT_EQ(s.cursor.x, 3);
    ASSERT_EQ(s.cursor.y, 2);

    screen_deinit(&s);
}

TEST(screen, blank_lines_above_the_cursor_are_kept) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 6, 0));

    /* A blank line somebody printed, with text after it. Only rows past the
     * cursor are spare room; a gap above it is content. */
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 0));
    s.cursor.page_cell->set_codepoint('A');
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 2));
    s.cursor.page_cell->set_codepoint('B');
    ASSERT_TRUE(screen_cursor_absolute(&s, 1, 2));

    ASSERT_TRUE(screen_resize(&s, 10, 6));

    ASSERT_EQ(screen_cell(&s, 0, 0)->codepoint(), 'A');
    ASSERT_EQ(screen_cell(&s, 0, 1)->codepoint(), 0u);
    ASSERT_EQ(screen_cell(&s, 0, 2)->codepoint(), 'B');

    screen_deinit(&s);
}

TEST(screen, trimming_stops_at_anything_pinned) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 6, 0));
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 0));

    /* A selection end, say, parked on a blank row below the cursor. It is not
     * spare room if something is holding on to it. */
    TrackedPin other;
    page_list_track(&s.pages, &other, page_list_active_pin(&s.pages, 0, 4));
    const size_t before = page_list_row_index(&s.pages, other.pin);

    screen_trim_blank_rows_below_cursor(&s);

    ASSERT_EQ(s.pages.row_count, before + 1);
    ASSERT_TRUE(other.pin.valid());

    page_list_untrack(&s.pages, &other);
    screen_deinit(&s);
}

TEST(screen, a_wrapped_line_below_the_cursor_is_kept) {
    Screen s;
    ASSERT_TRUE(screen_init(&s, 20, 6, 0));

    /* The last row is blank but continues a wrapped line, so it is part of
     * the text rather than the end of the screen. */
    ASSERT_TRUE(screen_cursor_absolute(&s, 0, 0));
    screen_row(&s, 4)->set_wrap(true);
    screen_row(&s, 5)->set_wrap_continuation(true);

    const size_t before = s.pages.row_count;
    screen_trim_blank_rows_below_cursor(&s);

    ASSERT_EQ(s.pages.row_count, before);

    screen_deinit(&s);
}
