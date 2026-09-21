/* Transliterated from the test blocks in Ghostty src/terminal/osc.zig,
 * src/terminal/osc/encoding.zig and, under src/terminal/osc/parsers/:
 * change_window_title.zig, change_window_icon.zig, hyperlink.zig,
 * report_pwd.zig and mouse_shape.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * These are upstream's tests: same inputs, same assertions, same names.
 *
 * Mapping from Zig, beyond test_parser_port.cpp's:
 *   Parser.init(null)             Parser p;            (no allocator)
 *   Parser.init(testing.allocator) Parser p(true);
 *   p.end(null)                   p.end()
 *   p.end('\x1b')                 p.end('\x1b')
 *   cmd == .change_window_title   cmd->key == Key::change_window_title
 *   cap.writer.buffer.len         cap.writer.capacity
 *   cap.trailing().len            cap.trailing_len()
 *
 * Not ported yet, because the parser they exercise is not yet
 * transliterated: "Parser nextSlice matches per-byte parsing" and "Parser
 * allocating capture limit includes parser-added bytes" (both OSC 52,
 * clipboard_operation.zig).
 */

#include "test_helpers.h"
#include "osc.hpp"

using namespace wisp::terminal::osc;

typedef Command::Key Key;

static void feed(Parser &p, const char *s) {
    for (const char *c = s; *c; c++) p.next((uint8_t)*c);
}

/* ─── osc.zig ────────────────────────────────────────────────────────────── */

TEST(osc, Parser_allocating_captures_have_a_hard_limit) {
    const char *prefixes[] = {"52;", "66;", "72;", "99;", "5522;"};
    const size_t limit = Parser::MAX_BUF + 1;

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        Parser p(true);
        p.max_allocating_bytes = limit;

        feed(p, prefixes[i]);
        for (size_t k = 0; k < limit; k++) p.next('a');

        Parser::Capture &cap = p.capture;
        ASSERT_TRUE(p.has_capture);
        ASSERT_EQ(cap.trailing_len(), limit);
        ASSERT_EQ(cap.writer.capacity, limit);

        p.next('a');
        ASSERT_TRUE(p.state == Parser::State::invalid);
        ASSERT_EQ(cap.trailing_len(), limit);
        ASSERT_EQ(cap.writer.capacity, limit);
    }
}

TEST(osc, Parser_nextSlice_allocating_captures_have_a_hard_limit) {
    const size_t limit = Parser::MAX_BUF + 1;

    Parser p(true);
    p.max_allocating_bytes = limit;

    char *data = (char *)malloc(limit);
    ASSERT_TRUE(data != nullptr);
    memset(data, 'a', limit);

    /* Exactly at the limit stays valid and bounded. */
    p.nextSlice("52;");
    p.nextSlice((const uint8_t *)data, limit);
    Parser::Capture &cap = p.capture;
    ASSERT_TRUE(p.state != Parser::State::invalid);
    ASSERT_EQ(cap.trailing_len(), limit);
    ASSERT_EQ(cap.writer.capacity, limit);

    /* One more byte overflows: the state becomes invalid and the
     * retained bytes and allocation stay bounded. */
    p.nextSlice("a");
    ASSERT_TRUE(p.state == Parser::State::invalid);
    ASSERT_EQ(cap.trailing_len(), limit);
    ASSERT_EQ(cap.writer.capacity, limit);
    ASSERT_TRUE(p.end() == nullptr);

    free(data);
}

TEST(osc, Parser_nextSlice_overflowing_slice_is_truncated_at_the_limit) {
    Parser p(true);
    p.max_allocating_bytes = 4;

    p.nextSlice("52;abcdef");
    ASSERT_TRUE(p.state == Parser::State::invalid);
    ASSERT_TRUE(p.end() == nullptr);

    Parser::Capture &cap = p.capture;
    ASSERT_EQ(cap.trailing_len(), 4u);
    ASSERT_TRUE(memcmp(cap.trailing(), "abcd", 4) == 0);
    ASSERT_EQ(cap.writer.capacity, 4u);
}

/* ─── encoding.zig ───────────────────────────────────────────────────────── */

TEST(encoding, isSafeUtf8) {
    ASSERT_TRUE(isSafeUtf8("Hello world!"));
    ASSERT_TRUE(isSafeUtf8("\xe5\xae\x89\xe5\x85\xa8\xe7\x9a\x84\xe3\x83\xa6"
                           "\xe3\x83\x8b\xe3\x82\xb3\xe3\x83\xbc\xe3\x83\x89"
                           "\xe2\x98\x80\xef\xb8\x8f"));
    ASSERT_FALSE(isSafeUtf8("No linebreaks\nallowed"));
    ASSERT_FALSE(isSafeUtf8("\x07no bells"));
    ASSERT_FALSE(isSafeUtf8("\x1b]9;no OSCs\x1b\\\x1b[m"));
    ASSERT_FALSE(isSafeUtf8("\x9f" "8-bit escapes are clever, but no"));
}

/* ─── parsers/change_window_title.zig ────────────────────────────────────── */

TEST(change_window_title, OSC_0_change_window_title) {
    Parser p;
    p.next('0');
    p.next(';');
    p.next('a');
    p.next('b');
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_title);
    ASSERT_TRUE(cmd->change_window_title.eql("ab"));
}

TEST(change_window_title, OSC_0_longer_than_buffer) {
    Parser p;

    p.next('0');
    p.next(';');
    for (size_t i = 0; i < Parser::MAX_BUF + 2; i++) p.next('a');

    ASSERT_TRUE(p.end() == nullptr);
}

TEST(change_window_title, OSC_0_one_shorter_than_buffer_length) {
    Parser p;

    p.next('0');
    p.next(';');
    for (size_t i = 0; i < Parser::MAX_BUF - 1; i++) p.next('a');

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_title);
    ASSERT_EQ(cmd->change_window_title.len, Parser::MAX_BUF - 1);
    for (size_t i = 0; i < cmd->change_window_title.len; i++) {
        ASSERT_EQ(cmd->change_window_title.ptr[i], 'a');
    }
}

TEST(change_window_title, OSC_0_exactly_at_buffer_length) {
    Parser p;

    p.next('0');
    p.next(';');
    for (size_t i = 0; i < Parser::MAX_BUF; i++) p.next('a');

    /* This should be null because we always reserve space for a null
     * terminator. */
    ASSERT_TRUE(p.end() == nullptr);
}

TEST(change_window_title, OSC_2_change_window_title_with_2) {
    Parser p;
    p.next('2');
    p.next(';');
    p.next('a');
    p.next('b');
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_title);
    ASSERT_TRUE(cmd->change_window_title.eql("ab"));
}

TEST(change_window_title, OSC_2_change_window_title_with_utf8) {
    Parser p;
    p.next('2');
    p.next(';');
    /* '—' EM DASH U+2014 (E2 80 94) */
    p.next(0xE2);
    p.next(0x80);
    p.next(0x94);

    p.next(' ');
    /* '‐' HYPHEN U+2010 (E2 80 90)
     * Intententionally chosen to conflict with the 0x90 C1 control */
    p.next(0xE2);
    p.next(0x80);
    p.next(0x90);
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_title);
    ASSERT_TRUE(cmd->change_window_title.eql("\xe2\x80\x94\x20\xe2\x80\x90"));
}

TEST(change_window_title, OSC_2_change_window_title_empty) {
    Parser p;
    p.next('2');
    p.next(';');
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_title);
    ASSERT_TRUE(cmd->change_window_title.eql(""));
}

/* ─── parsers/change_window_icon.zig ─────────────────────────────────────── */

TEST(change_window_icon, OSC_1_change_window_icon) {
    Parser p;
    p.next('1');
    p.next(';');
    p.next('a');
    p.next('b');
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::change_window_icon);
    ASSERT_TRUE(cmd->change_window_icon.eql("ab"));
}

/* ─── parsers/hyperlink.zig ──────────────────────────────────────────────── */

TEST(hyperlink, OSC_8_hyperlink) {
    Parser p;
    feed(p, "8;;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_id_set) {
    Parser p;
    feed(p, "8;id=foo;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_TRUE(cmd->hyperlink_start.has_id);
    ASSERT_TRUE(cmd->hyperlink_start.id.eql("foo"));
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_empty_id) {
    Parser p;
    feed(p, "8;id=;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_FALSE(cmd->hyperlink_start.has_id);
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_incomplete_key) {
    Parser p;
    feed(p, "8;id;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_FALSE(cmd->hyperlink_start.has_id);
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_empty_key) {
    Parser p;
    feed(p, "8;=value;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_FALSE(cmd->hyperlink_start.has_id);
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_empty_key_and_id) {
    Parser p;
    feed(p, "8;=value:id=foo;http://example.com");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_start);
    ASSERT_TRUE(cmd->hyperlink_start.has_id);
    ASSERT_TRUE(cmd->hyperlink_start.id.eql("foo"));
    ASSERT_TRUE(cmd->hyperlink_start.uri.eql("http://example.com"));
}

TEST(hyperlink, OSC_8_hyperlink_with_empty_uri) {
    Parser p;
    feed(p, "8;id=foo;");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd == nullptr);
}

TEST(hyperlink, OSC_8_hyperlink_end) {
    Parser p;
    feed(p, "8;;");

    const Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::hyperlink_end);
}

/* ─── parsers/report_pwd.zig ─────────────────────────────────────────────── */

TEST(report_pwd, OSC_7_report_pwd) {
    Parser p;
    feed(p, "7;file:///tmp/example");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::report_pwd);
    ASSERT_TRUE(cmd->report_pwd.value.eql("file:///tmp/example"));
}

TEST(report_pwd, OSC_7_report_pwd_empty) {
    Parser p;
    feed(p, "7;");
    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::report_pwd);
    ASSERT_TRUE(cmd->report_pwd.value.eql(""));
}

/* ─── parsers/mouse_shape.zig ────────────────────────────────────────────── */

TEST(mouse_shape, OSC_22_pointer_cursor) {
    Parser p;
    feed(p, "22;pointer");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::mouse_shape);
    ASSERT_TRUE(cmd->mouse_shape.value.eql("pointer"));
}
