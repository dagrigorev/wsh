/* Tests for src/terminal/terminal.hpp.
 *
 * Related to Ghostty src/terminal/Terminal.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * A screen has no opinions: it does not know that a line feed differs from
 * moving down, that a tab goes somewhere particular, or that DECAWM exists.
 * Those are properties of the machine being emulated, and these tests are
 * about them.
 *
 * Named test_terminal_port to keep it apart from the tests for the live C
 * emulator this does not yet replace.
 */

#include "test_helpers.h"
#include "terminal.hpp"

using namespace wisp::terminal;

static void write_text(Terminal *t, const char *text) {
    for (const char *c = text; *c; c++) {
        ASSERT_TRUE(terminal_print(t, (uint32_t)(unsigned char)*c, 1));
    }
}

static void read_row(Terminal *t, CellCountInt y, char *out, size_t out_len) {
    size_t n = 0;
    for (CellCountInt x = 0; x < t->cols && n + 1 < out_len; x++) {
        Cell *c = screen_cell(t->active, x, y);
        const uint32_t cp = c ? c->codepoint() : 0;
        out[n++] = cp ? (char)cp : ' ';
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

/* ─── setting up ─────────────────────────────────────────────────────────── */

TEST(terminal, starts_on_the_primary_screen) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));

    ASSERT_TRUE(t.active == &t.primary);
    ASSERT_FALSE(t.modes.alt_screen);
    ASSERT_TRUE(t.modes.wraparound);
    ASSERT_EQ(t.active->cursor.x, 0);

    terminal_deinit(&t);
}

TEST(terminal, both_screens_exist_from_the_start) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));

    /* Making the alternate one on demand would mean an allocation failing in
     * the middle of an escape sequence. */
    ASSERT_EQ(t.alternate.pages.cols, 20);
    ASSERT_TRUE(t.alternate.pages.no_scrollback);
    ASSERT_FALSE(t.primary.pages.no_scrollback);

    terminal_deinit(&t);
}

/* ─── printing ───────────────────────────────────────────────────────────── */

TEST(terminal, prints_to_the_active_screen) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));
    write_text(&t, "hello");

    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "hello") == 0);

    terminal_deinit(&t);
}

TEST(terminal, wraparound_off_keeps_the_cursor_at_the_margin) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 5, 4, 0));

    t.modes.wraparound = false;
    terminal_apply_modes(&t);
    write_text(&t, "abcdefg");

    /* The mode lives here now and the screen is told; it is not something a
     * caller sets on the screen behind the terminal's back. */
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abcdg") == 0);
    ASSERT_EQ(t.active->cursor.y, 0);

    terminal_deinit(&t);
}

TEST(terminal, insert_mode_pushes_the_line_right) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 10, 4, 0));
    write_text(&t, "abcdef");

    ASSERT_TRUE(screen_cursor_absolute(t.active, 2, 0));
    t.modes.insert = true;
    write_text(&t, "XY");

    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abXYcdef") == 0);

    terminal_deinit(&t);
}

TEST(terminal, insert_mode_drops_what_falls_off_the_end) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 6, 4, 0));
    write_text(&t, "abcdef");

    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));
    t.modes.insert = true;
    write_text(&t, "Z");

    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "Zabcde") == 0);

    terminal_deinit(&t);
}

/* ─── the C0 controls ────────────────────────────────────────────────────── */

TEST(terminal, carriage_return_stays_on_its_row) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));
    write_text(&t, "abc");

    terminal_carriage_return(&t);
    ASSERT_EQ(t.active->cursor.x, 0);
    ASSERT_EQ(t.active->cursor.y, 0);

    terminal_deinit(&t);
}

TEST(terminal, a_line_feed_keeps_the_column) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));
    write_text(&t, "abc");

    /* What distinguishes a line feed from a new line: the cursor lands
     * directly below where it was, and it is the CR that brings it back. */
    ASSERT_TRUE(terminal_linefeed(&t));
    ASSERT_EQ(t.active->cursor.x, 3);
    ASSERT_EQ(t.active->cursor.y, 1);

    terminal_deinit(&t);
}

TEST(terminal, a_line_feed_at_the_bottom_scrolls) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 3, 0));

    write_text(&t, "one");
    ASSERT_TRUE(terminal_linefeed(&t));
    terminal_carriage_return(&t);
    write_text(&t, "two");
    ASSERT_TRUE(terminal_linefeed(&t));
    terminal_carriage_return(&t);
    write_text(&t, "three");
    ASSERT_TRUE(terminal_linefeed(&t));
    terminal_carriage_return(&t);
    write_text(&t, "four");

    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "two") == 0);
    read_row(&t, 2, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "four") == 0);

    /* And "one" went to the scrollback rather than nowhere. */
    Pin old = page_list_pin(&t.active->pages, 0);
    old.x = 0;
    ASSERT_EQ(old.cell()->codepoint(), 'o');

    terminal_deinit(&t);
}

TEST(terminal, backspace_stops_at_the_left_margin) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));

    terminal_backspace(&t);
    ASSERT_EQ(t.active->cursor.x, 0);

    write_text(&t, "ab");
    terminal_backspace(&t);
    ASSERT_EQ(t.active->cursor.x, 1);

    terminal_deinit(&t);
}

TEST(terminal, reverse_index_moves_up) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 5, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 4, 3));

    ASSERT_TRUE(terminal_reverse_index(&t));
    ASSERT_EQ(t.active->cursor.y, 2);
    ASSERT_EQ(t.active->cursor.x, 4);

    terminal_deinit(&t);
}

TEST(terminal, reverse_index_at_the_top_scrolls_down) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 3, 0));

    write_text(&t, "one");
    ASSERT_TRUE(terminal_linefeed(&t));
    terminal_carriage_return(&t);
    write_text(&t, "two");
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));

    ASSERT_TRUE(terminal_reverse_index(&t));

    /* A blank line appears above, which is what lets a program insert a line
     * at the top without redrawing everything below it. */
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
    read_row(&t, 1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "one") == 0);
    read_row(&t, 2, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "two") == 0);

    terminal_deinit(&t);
}

TEST(terminal, reverse_index_does_not_make_scrollback) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 3, 0));
    write_text(&t, "bottom");
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 2));
    write_text(&t, "gone");
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));

    const size_t before = t.active->pages.row_count;
    ASSERT_TRUE(terminal_reverse_index(&t));

    /* The bottom row is lost rather than saved: it never left the top of the
     * screen, so it was never scrollback. */
    ASSERT_EQ(t.active->pages.row_count, before);
    char got[32];
    read_row(&t, 2, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);

    terminal_deinit(&t);
}

/* ─── tab stops ──────────────────────────────────────────────────────────── */

TEST(tabs, default_to_every_eighth_column) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 30, 4, 0));

    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 8);
    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 16);

    terminal_deinit(&t);
}

TEST(tabs, a_tab_moves_without_writing) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 30, 4, 0));
    write_text(&t, "0123456789");
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));

    terminal_horizontal_tab(&t, 1);

    /* A tab moves the cursor; it does not write spaces. Anything it passes
     * over is still there, which is how a program can tab back over its own
     * output. */
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "0123456789") == 0);
    ASSERT_EQ(t.active->cursor.x, 8);

    terminal_deinit(&t);
}

TEST(tabs, stop_at_the_last_column_when_there_are_no_more) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 17, 0));

    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 19);
    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 19);

    terminal_deinit(&t);
}

TEST(tabs, several_at_once) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 40, 4, 0));

    terminal_horizontal_tab(&t, 3);
    ASSERT_EQ(t.active->cursor.x, 24);

    terminal_deinit(&t);
}

TEST(tabs, setting_one_where_the_cursor_is) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 30, 4, 0));

    ASSERT_TRUE(screen_cursor_absolute(t.active, 3, 0));
    terminal_tab_set(&t);
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));

    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 3);

    terminal_deinit(&t);
}

TEST(tabs, clearing_one_and_clearing_all) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 30, 4, 0));

    ASSERT_TRUE(screen_cursor_absolute(t.active, 8, 0));
    terminal_tab_clear(&t, 0);
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));
    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 16);

    terminal_tab_clear(&t, 3);
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));
    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 29);

    terminal_deinit(&t);
}

TEST(tabs, going_backwards) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 30, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 20, 0));

    terminal_reverse_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 16);
    terminal_reverse_tab(&t, 2);
    ASSERT_EQ(t.active->cursor.x, 0);

    terminal_deinit(&t);
}

/* ─── the alternate screen ───────────────────────────────────────────────── */

TEST(alt, switching_shows_a_blank_screen) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    write_text(&t, "shell output");

    terminal_alt_screen_enter(&t);

    ASSERT_TRUE(t.active == &t.alternate);
    ASSERT_TRUE(t.modes.alt_screen);
    ASSERT_EQ(t.active->cursor.x, 0);
    ASSERT_EQ(t.active->cursor.y, 0);

    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);

    terminal_deinit(&t);
}

TEST(alt, the_primary_screen_is_untouched) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    write_text(&t, "shell output");

    terminal_alt_screen_enter(&t);
    write_text(&t, "editor");
    terminal_alt_screen_leave(&t);

    /* The whole point: what was on the screen before is still there, which is
     * why a shell prompt survives running an editor. */
    ASSERT_TRUE(t.active == &t.primary);
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "shell output") == 0);

    terminal_deinit(&t);
}

TEST(alt, the_cursor_comes_back_where_it_was) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 7, 2));

    terminal_alt_screen_enter(&t);
    ASSERT_TRUE(screen_cursor_absolute(t.active, 1, 1));
    terminal_alt_screen_leave(&t);

    /* Saving the cursor on the way in is what ?1049 does and ?47 did not,
     * and it is why ?1049 is the one everything uses. */
    ASSERT_EQ(t.active->cursor.x, 7);
    ASSERT_EQ(t.active->cursor.y, 2);

    terminal_deinit(&t);
}

TEST(alt, is_cleared_each_time_it_is_entered) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));

    terminal_alt_screen_enter(&t);
    write_text(&t, "first run");
    terminal_alt_screen_leave(&t);

    terminal_alt_screen_enter(&t);
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);

    terminal_deinit(&t);
}

TEST(alt, keeps_no_scrollback) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    terminal_alt_screen_enter(&t);

    /* Scrollback here would be a record of half-drawn frames rather than of
     * output, since the programs that use it redraw whenever they like. */
    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(screen_cursor_absolute(t.active, 0, t.active->cursor.y));
        ASSERT_TRUE(terminal_linefeed(&t));
    }

    ASSERT_EQ(page_list_max_scroll(&t.active->pages), 0u);
    ASSERT_EQ(t.active->pages.row_count, 4u);

    terminal_deinit(&t);
}

TEST(alt, the_primary_screen_keeps_its_scrollback) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 3, 0));
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(screen_cursor_absolute(&t.primary, 0, t.primary.cursor.y));
        ASSERT_TRUE(terminal_linefeed(&t));
    }
    const size_t before = page_list_max_scroll(&t.primary.pages);
    ASSERT_TRUE(before > 0u);

    terminal_alt_screen_enter(&t);
    for (int i = 0; i < 20; i++) {
        ASSERT_TRUE(screen_cursor_absolute(t.active, 0, t.active->cursor.y));
        ASSERT_TRUE(terminal_linefeed(&t));
    }
    terminal_alt_screen_leave(&t);

    ASSERT_EQ(page_list_max_scroll(&t.primary.pages), before);

    terminal_deinit(&t);
}

TEST(alt, entering_twice_is_not_two_saves) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 5, 1));

    terminal_alt_screen_enter(&t);
    terminal_alt_screen_enter(&t);
    terminal_alt_screen_leave(&t);

    ASSERT_EQ(t.active->cursor.x, 5);
    ASSERT_EQ(t.active->cursor.y, 1);

    terminal_deinit(&t);
}

TEST(alt, leaving_when_not_there_does_nothing) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    write_text(&t, "text");

    terminal_alt_screen_leave(&t);

    ASSERT_TRUE(t.active == &t.primary);
    ASSERT_EQ(t.active->cursor.x, 4);

    terminal_deinit(&t);
}

/* ─── resizing ───────────────────────────────────────────────────────────── */

TEST(terminal, resizing_touches_both_screens) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));
    write_text(&t, "primary text");

    terminal_alt_screen_enter(&t);
    ASSERT_TRUE(terminal_resize(&t, 9, 6));

    /* The screen that is not showing is resized too: a program on the
     * alternate screen expects the shell underneath to know the new size by
     * the time it exits. */
    ASSERT_EQ(t.cols, 9);
    ASSERT_EQ(t.primary.pages.cols, 9);
    ASSERT_EQ(t.alternate.pages.cols, 9);

    terminal_alt_screen_leave(&t);
    char got[32];
    read_row(&t, 0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "primary t") == 0);

    terminal_deinit(&t);
}

TEST(terminal, resizing_resets_the_tab_stops) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 40, 4, 0));
    ASSERT_TRUE(screen_cursor_absolute(t.active, 3, 0));
    terminal_tab_set(&t);

    ASSERT_TRUE(terminal_resize(&t, 20, 4));

    /* A stop is a column number, and the columns have changed. */
    ASSERT_TRUE(screen_cursor_absolute(t.active, 0, 0));
    terminal_horizontal_tab(&t, 1);
    ASSERT_EQ(t.active->cursor.x, 8);

    terminal_deinit(&t);
}

TEST(terminal, resizing_to_nothing_is_refused) {
    Terminal t;
    ASSERT_TRUE(terminal_init(&t, 20, 4, 0));

    ASSERT_FALSE(terminal_resize(&t, 0, 4));
    ASSERT_FALSE(terminal_resize(&t, 20, 0));
    ASSERT_EQ(t.cols, 20);

    terminal_deinit(&t);
}
