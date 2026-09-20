/* Tests for src/terminal/stream.hpp.
 *
 * Related to Ghostty src/terminal/stream.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Bytes in one end, a terminal that has changed at the other. These are the
 * first tests in the port that look like using a terminal rather than like
 * calling a function on one, which is the point of the layer: everything
 * underneath can now be driven the way a program actually drives it.
 */

#include "test_helpers.h"
#include "stream.hpp"

using namespace wisp::terminal;

struct Fixture {
    Terminal t;
    Stream   s;

    bool init(CellCountInt cols, CellCountInt rows) {
        if (!terminal_init(&t, cols, rows, 0)) return false;
        stream_init(&s, &t);
        return true;
    }

    void feed(const char *text) {
        stream_feed_text(&s, text, strlen(text));
    }

    void row(CellCountInt y, char *out, size_t cap) {
        size_t n = 0;
        for (CellCountInt x = 0; x < t.cols && n + 1 < cap; x++) {
            Cell *c = screen_cell(t.active, x, y);
            const uint32_t cp = c ? c->codepoint() : 0;
            out[n++] = cp ? (char)cp : ' ';
        }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = '\0';
    }

    style::Style style_at(CellCountInt x, CellCountInt y) {
        Pin p = page_list_active_pin(&t.active->pages, 0, y);
        return p.node->page.get_cell_style(x, p.y);
    }

    ~Fixture() { terminal_deinit(&t); }
};

/* ─── text and controls ──────────────────────────────────────────────────── */

TEST(stream, plain_text) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("hello");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "hello") == 0);
}

TEST(stream, newlines_and_returns) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("one\r\ntwo\r\nthree");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "one") == 0);
    f.row(2, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "three") == 0);
}

TEST(stream, a_bare_line_feed_keeps_the_column) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("abc\ndef");

    /* Without a carriage return the second line starts under the end of the
     * first, which is what a terminal does and what surprises people. */
    char got[32];
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "   def") == 0);
}

TEST(stream, tabs_and_backspace) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("a\tb\x08" "c");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "a       c") == 0);
}

/* ─── UTF-8 ──────────────────────────────────────────────────────────────── */

TEST(utf8, decodes_multibyte_characters) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("caf\xc3\xa9");

    ASSERT_EQ(screen_cell(f.t.active, 3, 0)->codepoint(), 0xE9u);
    ASSERT_EQ(f.t.active->cursor.x, 4);
}

TEST(utf8, survives_being_cut_mid_character) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));

    /* A read can end anywhere, including between the bytes of one character,
     * which is the same problem the parser has and needs the same answer. */
    stream_feed_text(&f.s, "\xc3", 1);
    ASSERT_EQ(f.t.active->cursor.x, 0);
    stream_feed_text(&f.s, "\xa9", 1);

    ASSERT_EQ(screen_cell(f.t.active, 0, 0)->codepoint(), 0xE9u);
}

TEST(utf8, a_wide_character_takes_two_cells) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\xe4\xb8\xad");

    ASSERT_EQ(screen_cell(f.t.active, 0, 0)->codepoint(), 0x4E2Du);
    ASSERT_TRUE(screen_cell(f.t.active, 1, 0)->wide() == Wide::spacer_tail);
    ASSERT_EQ(f.t.active->cursor.x, 2);
}

TEST(utf8, a_combining_mark_takes_no_cell) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("e\xcc\x81");

    /* It joins the character before it rather than taking a cell of its
     * own. */
    ASSERT_EQ(f.t.active->cursor.x, 1);
    Pin p = page_list_active_pin(&f.t.active->pages, 0, 0);
    ASSERT_TRUE(p.node->page.get_cell(0, p.y)->has_grapheme());
}

TEST(utf8, bad_bytes_become_the_replacement_character) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("a\xff" "b");

    /* Swallowing them would leave a user looking at output with a hole in it
     * and no reason for it. */
    ASSERT_EQ(screen_cell(f.t.active, 0, 0)->codepoint(), 'a');
    ASSERT_EQ(screen_cell(f.t.active, 1, 0)->codepoint(), 0xFFFDu);
    ASSERT_EQ(screen_cell(f.t.active, 2, 0)->codepoint(), 'b');
}

TEST(utf8, a_truncated_character_does_not_eat_what_follows) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\xe4\xb8" "x");

    /* What was collected is unreadable and says so, and the byte that
     * interrupted it starts a new character rather than being absorbed into
     * the broken one. */
    ASSERT_EQ(screen_cell(f.t.active, 0, 0)->codepoint(), 0xFFFDu);
    ASSERT_EQ(screen_cell(f.t.active, 1, 0)->codepoint(), 'x');
    ASSERT_EQ(f.t.active->cursor.x, 2);
}

/* ─── cursor movement ────────────────────────────────────────────────────── */

TEST(csi, cursor_movement) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[3;5H");

    /* CUP counts from one, which is the one place these coordinates are not
     * the ones everything else here uses. */
    ASSERT_EQ(f.t.active->cursor.y, 2);
    ASSERT_EQ(f.t.active->cursor.x, 4);

    f.feed("\x1b[2A\x1b[3C");
    ASSERT_EQ(f.t.active->cursor.y, 0);
    ASSERT_EQ(f.t.active->cursor.x, 7);
}

TEST(csi, movement_with_no_parameter_moves_one) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[5;5H\x1b[B\x1b[D");

    ASSERT_EQ(f.t.active->cursor.y, 5);
    ASSERT_EQ(f.t.active->cursor.x, 3);
}

TEST(csi, home_with_no_parameters) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[4;4H\x1b[H");

    ASSERT_EQ(f.t.active->cursor.x, 0);
    ASSERT_EQ(f.t.active->cursor.y, 0);
}

TEST(csi, column_and_row_addressing) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[10G");
    ASSERT_EQ(f.t.active->cursor.x, 9);

    f.feed("\x1b[4d");
    ASSERT_EQ(f.t.active->cursor.y, 3);
    ASSERT_EQ(f.t.active->cursor.x, 9);
}

/* ─── erasing ────────────────────────────────────────────────────────────── */

TEST(csi, erase_in_line) {
    Fixture f;
    ASSERT_TRUE(f.init(10, 4));
    f.feed("abcdefgh\x1b[1;4H\x1b[K");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abc") == 0);
}

TEST(csi, erase_in_display) {
    Fixture f;
    ASSERT_TRUE(f.init(10, 4));
    f.feed("one\r\ntwo\r\nthree\x1b[H\x1b[J");

    char got[32];
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
}

TEST(csi, erase_the_whole_screen) {
    Fixture f;
    ASSERT_TRUE(f.init(10, 4));
    f.feed("text\r\nmore\x1b[2J");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
}

/* ─── SGR ────────────────────────────────────────────────────────────────── */

TEST(sgr, sets_a_colour) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[31m" "red");

    const style::Style st = f.style_at(0, 0);
    ASSERT_TRUE(st.fg_color.tag == style::StyleColor::Tag::palette);
    ASSERT_EQ(st.fg_color.palette, 1);
}

TEST(sgr, sets_flags) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[1;3;4m" "x");

    const style::Style st = f.style_at(0, 0);
    ASSERT_TRUE(st.flags.bold);
    ASSERT_TRUE(st.flags.italic);
    ASSERT_TRUE(st.flags.underline == Underline::single);
}

TEST(sgr, reset_clears_everything) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[1;31m" "a" "\x1b[0m" "b");

    ASSERT_TRUE(f.style_at(0, 0).flags.bold);
    ASSERT_TRUE(f.style_at(1, 0).is_default());
}

TEST(sgr, an_empty_sequence_is_a_reset) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[1;31m" "a" "\x1b[m" "b");

    ASSERT_TRUE(f.style_at(1, 0).is_default());
}

TEST(sgr, twenty_two_turns_off_faint_as_well) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[1;2m" "a" "\x1b[22m" "b");

    /* A quirk of the standard rather than an oversight here. */
    ASSERT_TRUE(f.style_at(0, 0).flags.bold);
    ASSERT_TRUE(f.style_at(0, 0).flags.faint);
    ASSERT_FALSE(f.style_at(1, 0).flags.bold);
    ASSERT_FALSE(f.style_at(1, 0).flags.faint);
}

TEST(sgr, a_256_colour) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[38;5;196m" "x");

    const style::Style st = f.style_at(0, 0);
    ASSERT_TRUE(st.fg_color.tag == style::StyleColor::Tag::palette);
    ASSERT_EQ(st.fg_color.palette, 196);
}

TEST(sgr, a_direct_colour) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[48;2;10;20;30m" "x");

    const style::Style st = f.style_at(0, 0);
    ASSERT_TRUE(st.bg_color.tag == style::StyleColor::Tag::rgb);
    ASSERT_TRUE(st.bg_color.rgb.eql(RGB(10, 20, 30)));
}

TEST(sgr, colons_mean_what_they_did_in_the_parser) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[4:3m" "x");

    /* The separator survived the parser, the action and sgr.hpp to arrive
     * here as a curly underline rather than as underline-then-italic. */
    ASSERT_TRUE(f.style_at(0, 0).flags.underline == Underline::curly);
}

TEST(sgr, a_colour_written_with_colons) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[38:2::10:20:30m" "x");

    const style::Style st = f.style_at(0, 0);
    ASSERT_TRUE(st.fg_color.tag == style::StyleColor::Tag::rgb);
    ASSERT_TRUE(st.fg_color.rgb.eql(RGB(10, 20, 30)));
}

TEST(sgr, an_unknown_attribute_does_not_spoil_the_rest) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[1;99;31m" "x");

    /* A program setting one attribute nobody here implements still meant the
     * others. */
    ASSERT_TRUE(f.style_at(0, 0).flags.bold);
    ASSERT_EQ(f.style_at(0, 0).fg_color.palette, 1);
}

/* ─── modes ──────────────────────────────────────────────────────────────── */

TEST(modes, wraparound_off_and_on) {
    Fixture f;
    ASSERT_TRUE(f.init(5, 4));
    f.feed("\x1b[?7l" "abcdefg");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abcdg") == 0);

    f.feed("\x1b[?7h\x1b[2;1H" "abcdefg");
    f.row(2, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "fg") == 0);
}

TEST(modes, insert_mode) {
    Fixture f;
    ASSERT_TRUE(f.init(10, 4));
    f.feed("abcdef\x1b[1;3H\x1b[4h" "XY");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abXYcdef") == 0);
}

TEST(modes, the_alternate_screen) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("shell\x1b[?1049h" "editor");

    ASSERT_TRUE(f.t.active == &f.t.alternate);
    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "editor") == 0);

    f.feed("\x1b[?1049l");
    ASSERT_TRUE(f.t.active == &f.t.primary);
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "shell") == 0);
}

/* ─── scroll regions ─────────────────────────────────────────────────────── */

TEST(csi, sets_a_scroll_region) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[2;5r");

    /* DECSTBM counts from one; converting is this layer's job, which is why
     * the terminal's own bounds are zero-based like everything else. */
    ASSERT_EQ(f.t.scroll_top, 1);
    ASSERT_EQ(f.t.scroll_bot, 4);
    ASSERT_EQ(f.t.active->cursor.y, 0);
}

TEST(csi, a_region_with_no_parameters_is_the_whole_screen) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[2;5r\x1b[r");

    ASSERT_TRUE(terminal_region_is_whole_screen(&f.t));
}

TEST(csi, inserting_and_deleting_lines) {
    Fixture f;
    ASSERT_TRUE(f.init(10, 5));
    f.feed("a\r\nb\r\nc\x1b[1;1H\x1b[L");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "a") == 0);

    f.feed("\x1b[M");
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "a") == 0);
}

TEST(csi, inserting_and_deleting_characters) {
    Fixture f;
    ASSERT_TRUE(f.init(12, 4));
    f.feed("abcdef\x1b[1;3H\x1b[2@");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "ab  cdef") == 0);

    f.feed("\x1b[2P");
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "abcdef") == 0);
}

/* ─── escapes ────────────────────────────────────────────────────────────── */

TEST(esc, save_and_restore_the_cursor) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("\x1b[3;7H\x1b" "7" "\x1b[1;1H" "\x1b" "8");

    ASSERT_EQ(f.t.active->cursor.x, 6);
    ASSERT_EQ(f.t.active->cursor.y, 2);
}

TEST(esc, next_line_and_reverse_index) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 6));
    f.feed("abc\x1b" "E");

    ASSERT_EQ(f.t.active->cursor.x, 0);
    ASSERT_EQ(f.t.active->cursor.y, 1);

    f.feed("\x1b" "M");
    ASSERT_EQ(f.t.active->cursor.y, 0);
}

/* ─── what a real program sends ──────────────────────────────────────────── */

TEST(stream, a_prompt_and_some_output) {
    Fixture f;
    ASSERT_TRUE(f.init(40, 6));
    f.feed("\x1b[1;32m" "user@host" "\x1b[0m" ":" "\x1b[1;34m" "~" "\x1b[0m"
           "$ ls\r\n"
           "file.txt\r\n");

    char got[64];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "user@host:~$ ls") == 0);
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "file.txt") == 0);

    ASSERT_TRUE(f.style_at(0, 0).flags.bold);
    ASSERT_EQ(f.style_at(0, 0).fg_color.palette, 2);
    ASSERT_TRUE(f.style_at(9, 0).is_default());
    ASSERT_EQ(f.style_at(10, 0).fg_color.palette, 4);
}

TEST(stream, a_full_screen_program_leaves_no_trace) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("$ vim\r\n");
    f.feed("\x1b[?1049h\x1b[2J\x1b[1;1H" "~\r\n~\r\n~");
    f.feed("\x1b[?1049l");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "$ vim") == 0);
    f.row(1, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "") == 0);
}

TEST(stream, output_split_across_reads) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));

    /* The whole reason the parser and the decoder both keep their state in
     * fields. A pipe splits output wherever it likes. */
    const char *text = "\x1b[1;31m" "err" "\x1b[0m" "\xe4\xb8\xad";
    for (size_t i = 0; text[i]; i++) {
        stream_feed_text(&f.s, text + i, 1);
    }

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strncmp(got, "err", 3) == 0);
    ASSERT_TRUE(f.style_at(0, 0).flags.bold);
    ASSERT_EQ(screen_cell(f.t.active, 3, 0)->codepoint(), 0x4E2Du);
}

TEST(stream, scrolling_output_fills_the_scrollback) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 3));
    for (int i = 0; i < 10; i++) {
        char line[8];
        line[0] = (char)('0' + i);
        line[1] = '\r';
        line[2] = '\n';
        line[3] = '\0';
        f.feed(line);
    }

    ASSERT_TRUE(page_list_max_scroll(&f.t.primary.pages) > 0u);
    Pin old = page_list_pin(&f.t.primary.pages, 0);
    old.x = 0;
    ASSERT_EQ(old.cell()->codepoint(), '0');
}

TEST(stream, an_unknown_sequence_is_ignored_not_printed) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b[99999;1;2;3~" "after");

    /* Printing an escape sequence that was not understood is worse than
     * doing nothing with it. */
    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "after") == 0);
}

TEST(stream, an_osc_is_consumed) {
    Fixture f;
    ASSERT_TRUE(f.init(20, 4));
    f.feed("\x1b]0;my title\x07" "text");

    char got[32];
    f.row(0, got, sizeof(got));
    ASSERT_TRUE(strcmp(got, "text") == 0);
}
