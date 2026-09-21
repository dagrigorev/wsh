/* Transliterated from the test blocks in Ghostty src/terminal/osc.zig,
 * src/terminal/osc/encoding.zig and, under src/terminal/osc/parsers/:
 * change_window_title.zig, change_window_icon.zig, hyperlink.zig,
 * report_pwd.zig, mouse_shape.zig and clipboard_operation.zig
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
 */

#include "test_helpers.h"
#include "osc.hpp"

#include <string>

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

/* ─── osc.zig: the two that needed the clipboard parser ──────────────────── */

TEST(osc, Parser_nextSlice_matches_per_byte_parsing) {
    const char *input = "52;c;aGVsbG8=";
    const size_t len = strlen(input);

    /* Every two-way split of the input must parse identically to
     * the byte-at-a-time path. */
    for (size_t split = 0; split < len + 1; split++) {
        Parser p(true);
        p.nextSlice((const uint8_t *)input, split);
        p.nextSlice((const uint8_t *)input + split, len - split);

        const Command *cmd = p.end();
        ASSERT_TRUE(cmd != nullptr);
        ASSERT_TRUE(cmd->key == Key::clipboard_contents);
        ASSERT_EQ(cmd->clipboard_contents.kind, 'c');
        ASSERT_TRUE(cmd->clipboard_contents.data.eql("aGVsbG8="));
    }
}

TEST(osc, Parser_allocating_capture_limit_includes_parser_added_bytes) {
    Parser p(true);
    p.max_allocating_bytes = 4;

    feed(p, "52;abcd");
    ASSERT_TRUE(p.end() == nullptr);
    ASSERT_TRUE(p.state == Parser::State::invalid);

    Parser::Capture &cap = p.capture;
    ASSERT_EQ(cap.trailing_len(), 4u);
    ASSERT_EQ(cap.writer.capacity, 4u);
}

/* ─── parsers/clipboard_operation.zig ────────────────────────────────────── */

TEST(clipboard_operation, OSC_52_get_set_clipboard) {
    Parser p;
    feed(p, "52;s;?");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.kind == 's');
    ASSERT_TRUE(cmd->clipboard_contents.data.eql("?"));
    ASSERT_TRUE(cmd->clipboard_contents.terminator == Terminator::st);
}

TEST(clipboard_operation, OSC_52_get_clipboard_with_BEL_terminator) {
    Parser p;
    feed(p, "52;c;?");

    const Command *cmd = p.end(0x07);
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.terminator == Terminator::bel);
}

TEST(clipboard_operation, OSC_52_get_set_clipboard_optional_parameter) {
    Parser p;
    feed(p, "52;;?");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.kind == 'c');
    ASSERT_TRUE(cmd->clipboard_contents.data.eql("?"));
}

TEST(clipboard_operation, OSC_52_get_set_clipboard_with_allocator) {
    Parser p(true);
    feed(p, "52;s;?");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.kind == 's');
    ASSERT_TRUE(cmd->clipboard_contents.data.eql("?"));
}

TEST(clipboard_operation, OSC_52_clear_clipboard) {
    Parser p;
    feed(p, "52;;");

    const Command *cmd = p.end();
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.kind == 'c');
    ASSERT_TRUE(cmd->clipboard_contents.data.eql(""));
}

/* rxvt_extension.zig */

TEST(rxvt_extension, OSC_777_show_desktop_notification_with_title) {
    Parser p; /* .init(null) */

    const char *input = "777;notify;Title;Body";
    for (const char *c = input; *c; c++) p.next((uint8_t)*c);

    Command *cmd = p.end('\x1b');
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_TRUE(cmd->key == Command::Key::show_desktop_notification);
    ASSERT_TRUE(cmd->show_desktop_notification.title.eql("Title"));
    ASSERT_TRUE(cmd->show_desktop_notification.body.eql("Body"));
}

/* kitty_dnd_protocol.zig */

static Command *feed_end(Parser &p, const char *input, bool has_ch, uint8_t ch) {
    for (const char *c = input; *c; c++) p.next((uint8_t)*c);
    return has_ch ? p.end(ch) : p.end();
}

TEST(kitty_dnd_protocol, OSC_72_metadata_only_no_payload) {
    Parser p(true);
    Command *cmd = feed_end(p, "72;t=a", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_dnd_protocol);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.metadata.eql("t=a"));
    ASSERT_FALSE(cmd->kitty_dnd_protocol.has_payload);
}

TEST(kitty_dnd_protocol, OSC_72_metadata_and_empty_payload) {
    Parser p(true);
    Command *cmd = feed_end(p, "72;t=a;", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_dnd_protocol);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.metadata.eql("t=a"));
    ASSERT_TRUE(cmd->kitty_dnd_protocol.has_payload);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.payload.eql(""));
}

TEST(kitty_dnd_protocol, OSC_72_metadata_and_non_empty_payload) {
    Parser p(true);
    Command *cmd = feed_end(p, "72;t=a:i=5;text/plain text/uri-list", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_dnd_protocol);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.metadata.eql("t=a:i=5"));
    ASSERT_TRUE(cmd->kitty_dnd_protocol.has_payload);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.payload.eql("text/plain text/uri-list"));
}

TEST(kitty_dnd_protocol, OSC_72_empty_metadata_with_payload) {
    Parser p(true);
    Command *cmd = feed_end(p, "72;;payload", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_dnd_protocol);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.metadata.eql(""));
    ASSERT_TRUE(cmd->kitty_dnd_protocol.has_payload);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.payload.eql("payload"));
}

TEST(kitty_dnd_protocol, OSC_72_BEL_terminator_recorded) {
    Parser p(true);
    Command *cmd = feed_end(p, "72;t=q", true, 0x07);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_dnd_protocol);
    ASSERT_TRUE(cmd->kitty_dnd_protocol.terminator == Terminator::bel);
}

/* kitty_color.zig */

namespace kc = wisp::terminal::kitty::color;

static const char *kitty_color_input =
    "21;foreground=?;background=rgb:f0/f8/ff;cursor=aliceblue;cursor_text;"
    "visual_bell=;selection_foreground=#xxxyyzz;selection_background=?;"
    "selection_background=#aabbcc;2=?;3=rgbi:1.0/1.0/1.0";

TEST(kitty_color, OSC_21_kitty_color_protocol) {
    typedef kc::Kind Kind;
    typedef kc::Request::Tag RT;

    Parser p(true);
    Command *cmd = feed_end(p, kitty_color_input, true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_color_protocol);
    const kc::RequestList &list = cmd->kitty_color_protocol.list;
    ASSERT_TRUE(list.len == 9);
    {
        const kc::Request &item = list.items[0];
        ASSERT_TRUE(item.tag == RT::query);
        ASSERT_TRUE(item.query.eql(Kind::makeSpecial(kc::Special::foreground)));
    }
    {
        const kc::Request &item = list.items[1];
        ASSERT_TRUE(item.tag == RT::set);
        ASSERT_TRUE(item.set.key.eql(Kind::makeSpecial(kc::Special::background)));
        ASSERT_TRUE(item.set.color.r == 0xf0);
        ASSERT_TRUE(item.set.color.g == 0xf8);
        ASSERT_TRUE(item.set.color.b == 0xff);
    }
    {
        const kc::Request &item = list.items[2];
        ASSERT_TRUE(item.tag == RT::set);
        ASSERT_TRUE(item.set.key.eql(Kind::makeSpecial(kc::Special::cursor)));
        ASSERT_TRUE(item.set.color.r == 0xf0);
        ASSERT_TRUE(item.set.color.g == 0xf8);
        ASSERT_TRUE(item.set.color.b == 0xff);
    }
    {
        const kc::Request &item = list.items[3];
        ASSERT_TRUE(item.tag == RT::reset);
        ASSERT_TRUE(item.reset.eql(Kind::makeSpecial(kc::Special::cursor_text)));
    }
    {
        const kc::Request &item = list.items[4];
        ASSERT_TRUE(item.tag == RT::reset);
        ASSERT_TRUE(item.reset.eql(Kind::makeSpecial(kc::Special::visual_bell)));
    }
    {
        const kc::Request &item = list.items[5];
        ASSERT_TRUE(item.tag == RT::query);
        ASSERT_TRUE(item.query.eql(Kind::makeSpecial(kc::Special::selection_background)));
    }
    {
        const kc::Request &item = list.items[6];
        ASSERT_TRUE(item.tag == RT::set);
        ASSERT_TRUE(item.set.key.eql(Kind::makeSpecial(kc::Special::selection_background)));
        ASSERT_TRUE(item.set.color.r == 0xaa);
        ASSERT_TRUE(item.set.color.g == 0xbb);
        ASSERT_TRUE(item.set.color.b == 0xcc);
    }
    {
        const kc::Request &item = list.items[7];
        ASSERT_TRUE(item.tag == RT::query);
        ASSERT_TRUE(item.query.eql(Kind::makePalette(2)));
    }
    {
        const kc::Request &item = list.items[8];
        ASSERT_TRUE(item.tag == RT::set);
        ASSERT_TRUE(item.set.key.eql(Kind::makePalette(3)));
        ASSERT_TRUE(item.set.color.r == 0xff);
        ASSERT_TRUE(item.set.color.g == 0xff);
        ASSERT_TRUE(item.set.color.b == 0xff);
    }
}

TEST(kitty_color, OSC_21_kitty_color_protocol_without_allocator) {
    Parser p; /* .init(null) */
    ASSERT_TRUE(feed_end(p, "21;foreground=?", true, '\x1b') == nullptr);
}

TEST(kitty_color, OSC_21_kitty_color_protocol_double_reset) {
    Parser p(true);
    Command *cmd = feed_end(p, kitty_color_input, true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_color_protocol);

    p.reset();
    p.reset();
}

TEST(kitty_color, OSC_21_kitty_color_protocol_reset_after_invalid) {
    Parser p(true);
    Command *cmd = feed_end(p, kitty_color_input, true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_color_protocol);

    p.reset();

    ASSERT_TRUE(p.state == Parser::State::start);
    p.next('X');
    ASSERT_TRUE(p.state == Parser::State::invalid);

    p.reset();
}

TEST(kitty_color, OSC_21_kitty_color_protocol_no_key) {
    Parser p(true);
    Command *cmd = feed_end(p, "21;", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_color_protocol);
    ASSERT_TRUE(cmd->kitty_color_protocol.list.len == 0);
}

/* kitty/color.zig */

TEST(kitty_color, OSC_kitty_color_protocol_kind_string) {
    char buf[256];
    {
        const size_t n = kc::Kind::makeSpecial(kc::Special::foreground).format(buf, sizeof(buf));
        ASSERT_TRUE(n == 10 && strcmp(buf, "foreground") == 0);
    }
    {
        const size_t n = kc::Kind::makePalette(42).format(buf, sizeof(buf));
        ASSERT_TRUE(n == 2 && strcmp(buf, "42") == 0);
    }

    ASSERT_TRUE(kc::Kind::makePalette(42).hasTerminalQueryColor());
    ASSERT_TRUE(kc::Kind::makeSpecial(kc::Special::foreground).hasTerminalQueryColor());
    ASSERT_FALSE(kc::Kind::makeSpecial(kc::Special::selection_background).hasTerminalQueryColor());
}

/* kitty_text_sizing.zig */

namespace kts = wisp::terminal::osc::kitty_text_sizing;

TEST(kitty_text_sizing, OSC_66_empty_parameters) {
    Parser p; /* .init(null) */
    Command *cmd = feed_end(p, "66;;bobr", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.scale == 1);
    ASSERT_TRUE(cmd->kitty_text_sizing.text.eql("bobr"));
}

TEST(kitty_text_sizing, OSC_66_single_parameter) {
    Parser p;
    Command *cmd = feed_end(p, "66;s=2;kurwa", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.scale == 2);
    ASSERT_TRUE(cmd->kitty_text_sizing.text.eql("kurwa"));
}

TEST(kitty_text_sizing, OSC_66_multiple_parameters) {
    Parser p;
    Command *cmd = feed_end(p, "66;s=2:w=7:n=13:d=15:v=1:h=2;long", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.scale == 2);
    ASSERT_TRUE(cmd->kitty_text_sizing.width == 7);
    ASSERT_TRUE(cmd->kitty_text_sizing.numerator == 13);
    ASSERT_TRUE(cmd->kitty_text_sizing.denominator == 15);
    ASSERT_TRUE(cmd->kitty_text_sizing.valign == kts::VAlign::bottom);
    ASSERT_TRUE(cmd->kitty_text_sizing.halign == kts::HAlign::center);
    ASSERT_TRUE(cmd->kitty_text_sizing.text.eql("long"));
}

TEST(kitty_text_sizing, OSC_66_scale_is_zero) {
    Parser p;
    Command *cmd = feed_end(p, "66;s=0;nope", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.scale == 1);
}

TEST(kitty_text_sizing, OSC_66_invalid_parameters) {
    Parser p;
    Command *cmd = feed_end(p, "66;w=8:v=3:n=16;", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.width == 0);
    ASSERT_TRUE(cmd->kitty_text_sizing.valign == kts::VAlign::top);
    ASSERT_TRUE(cmd->kitty_text_sizing.numerator == 0);
}

TEST(kitty_text_sizing, OSC_66_UTF_8) {
    Parser p;
    Command *cmd = feed_end(p, "66;;\360\237\221\273\351\255\221\351\255\205\351\255\215\351\255\211\343\202\264\343\203\274\343\202\271\343\203\203\343\203\206\343\202\243", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::kitty_text_sizing);
    ASSERT_TRUE(cmd->kitty_text_sizing.text.eql("\360\237\221\273\351\255\221\351\255\205\351\255\215\351\255\211\343\202\264\343\203\274\343\202\271\343\203\203\343\203\206\343\202\243"));
}

TEST(kitty_text_sizing, OSC_66_unsafe_UTF_8) {
    Parser p;
    ASSERT_TRUE(feed_end(p, "66;;\n", true, '\x1b') == nullptr);
}

TEST(kitty_text_sizing, OSC_66_overlong_UTF_8) {
    Parser p;
    for (const char *c = "66;;"; *c; c++) p.next((uint8_t)*c);
    for (int i = 0; i < 1025; i++) {
        for (const char *c = "bobr"; *c; c++) p.next((uint8_t)*c);
    }
    ASSERT_TRUE(p.end('\x1b') == nullptr);
}

/* context_signal.zig */

namespace cs = wisp::terminal::osc::context_signal;

static std::string repeat_a(size_t n) { return std::string(n, 'a'); }

TEST(context_signal, OSC_3008_basic_start_command) {
    Parser p; /* .init(null) */
    Command *cmd = feed_end(p, "3008;start=abc123", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::start);
    ASSERT_TRUE(cmd->context_signal.id.eql("abc123"));
    ASSERT_TRUE(cmd->context_signal.metadata.eql(""));
}

TEST(context_signal, OSC_3008_basic_end_command) {
    Parser p;
    Command *cmd = feed_end(p, "3008;end=abc123", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::end);
    ASSERT_TRUE(cmd->context_signal.id.eql("abc123"));
    ASSERT_TRUE(cmd->context_signal.metadata.eql(""));
}

TEST(context_signal, OSC_3008_start_with_metadata_fields) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=bed86fab93af4328bbed0a1224af6d40;type=container;user=lennart;hostname=zeta", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::start);
    ASSERT_TRUE(cmd->context_signal.id.eql("bed86fab93af4328bbed0a1224af6d40"));

    /* Read individual fields */
    cs::ContextType t;
    ZStr v;
    ASSERT_TRUE(cmd->context_signal.readType(&t) && t == cs::ContextType::container);
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::user, &v) && v.eql("lennart"));
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::hostname, &v) && v.eql("zeta"));
}

TEST(context_signal, OSC_3008_start_with_all_common_fields) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=myctx;type=shell;user=root;hostname=myhost;machineid=3deb5353d3ba43d08201c136a47ead7b;bootid=d4a3d0fdf2e24fdea6d971ce73f4fbf2;pid=1062862;pidfdid=1063162;comm=bash", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    const cs::Command &c = cmd->context_signal;
    cs::ContextType t;
    ZStr v;
    uint64_t n;
    ASSERT_TRUE(c.readType(&t) && t == cs::ContextType::shell);
    ASSERT_TRUE(c.readString(cs::Field::user, &v) && v.eql("root"));
    ASSERT_TRUE(c.readString(cs::Field::hostname, &v) && v.eql("myhost"));
    ASSERT_TRUE(c.readString(cs::Field::machineid, &v) && v.eql("3deb5353d3ba43d08201c136a47ead7b"));
    ASSERT_TRUE(c.readString(cs::Field::bootid, &v) && v.eql("d4a3d0fdf2e24fdea6d971ce73f4fbf2"));
    ASSERT_TRUE(c.readU64(cs::Field::pid, &n) && n == 1062862);
    ASSERT_TRUE(c.readU64(cs::Field::pidfdid, &n) && n == 1063162);
    ASSERT_TRUE(c.readString(cs::Field::comm, &v) && v.eql("bash"));
}

TEST(context_signal, OSC_3008_end_with_exit_metadata) {
    Parser p;
    Command *cmd = feed_end(p, "3008;end=myctx;exit=success;status=0", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::end);
    ASSERT_TRUE(cmd->context_signal.id.eql("myctx"));
    cs::ExitStatus e;
    uint64_t n;
    ASSERT_TRUE(cmd->context_signal.readExit(&e) && e == cs::ExitStatus::success);
    ASSERT_TRUE(cmd->context_signal.readU64(cs::Field::status, &n) && n == 0);
}

TEST(context_signal, OSC_3008_end_with_failure_exit) {
    Parser p;
    Command *cmd = feed_end(p, "3008;end=myctx;exit=failure;status=1;signal=SIGKILL", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    cs::ExitStatus e;
    uint64_t n;
    ZStr v;
    ASSERT_TRUE(cmd->context_signal.readExit(&e) && e == cs::ExitStatus::failure);
    ASSERT_TRUE(cmd->context_signal.readU64(cs::Field::status, &n) && n == 1);
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::signal, &v) && v.eql("SIGKILL"));
}

TEST(context_signal, OSC_3008_unknown_fields_are_ignored) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=myctx;type=shell;unknownfield=value;user=root", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    cs::ContextType t;
    ZStr v;
    ASSERT_TRUE(cmd->context_signal.readType(&t) && t == cs::ContextType::shell);
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::user, &v) && v.eql("root"));
}

TEST(context_signal, OSC_3008_missing_field_returns_null) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=myctx;user=lennart", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    cs::ContextType t;
    ZStr v;
    uint64_t n;
    ASSERT_FALSE(cmd->context_signal.readType(&t));
    ASSERT_FALSE(cmd->context_signal.readString(cs::Field::hostname, &v));
    ASSERT_FALSE(cmd->context_signal.readU64(cs::Field::pid, &n));
}

TEST(context_signal, OSC_3008_invalid_prefix) {
    Parser p;
    ASSERT_TRUE(feed_end(p, "3008;bogus=abc123", false, 0) == nullptr);
}

TEST(context_signal, OSC_3008_empty_data) {
    /* Can't really produce empty data after "3008;" because the state machine
     * won't write a writer for that case, but we test the edge case where
     * only "start=" is present with no ID. */
    Parser p;
    ASSERT_TRUE(feed_end(p, "3008;start=", false, 0) == nullptr);
}

TEST(context_signal, OSC_3008_max_length_context_ID) {
    Parser p;
    const std::string id = repeat_a(64);
    const std::string input = "3008;start=" + id;
    Command *cmd = feed_end(p, input.c_str(), false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.id.eql(id.c_str()));
}

TEST(context_signal, OSC_3008_over_length_context_ID) {
    Parser p;
    const std::string input = "3008;start=" + repeat_a(65);
    ASSERT_TRUE(feed_end(p, input.c_str(), false, 0) == nullptr);
}

TEST(context_signal, OSC_3008_context_type_enum_coverage) {
    struct T { const char *str; cs::ContextType expected; };
    const T types[] = {
        { "boot", cs::ContextType::boot },
        { "container", cs::ContextType::container },
        { "vm", cs::ContextType::vm },
        { "elevate", cs::ContextType::elevate },
        { "chpriv", cs::ContextType::chpriv },
        { "subcontext", cs::ContextType::subcontext },
        { "remote", cs::ContextType::remote },
        { "shell", cs::ContextType::shell },
        { "command", cs::ContextType::command },
        { "app", cs::ContextType::app },
        { "service", cs::ContextType::service },
        { "session", cs::ContextType::session },
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        cs::ContextType t;
        ASSERT_TRUE(cs::ContextType_parse(types[i].str, &t) && t == types[i].expected);
    }

    cs::ContextType t;
    ASSERT_FALSE(cs::ContextType_parse("invalid", &t));
}

TEST(context_signal, OSC_3008_exit_status_enum_coverage) {
    cs::ExitStatus e;
    ASSERT_TRUE(cs::ExitStatus_parse("success", &e) && e == cs::ExitStatus::success);
    ASSERT_TRUE(cs::ExitStatus_parse("failure", &e) && e == cs::ExitStatus::failure);
    ASSERT_TRUE(cs::ExitStatus_parse("crash", &e) && e == cs::ExitStatus::crash);
    ASSERT_TRUE(cs::ExitStatus_parse("interrupt", &e) && e == cs::ExitStatus::interrupt);
    ASSERT_FALSE(cs::ExitStatus_parse("invalid", &e));
}

TEST(context_signal, OSC_3008_spec_example_container_start) {
    /* From the spec: a new container "foobar" invoked by user "lennart" on host "zeta" */
    Parser p;
    Command *cmd = feed_end(p, "3008;start=bed86fab93af4328bbed0a1224af6d40;type=container;user=lennart;hostname=zeta;machineid=3deb5353d3ba43d08201c136a47ead7b;bootid=d4a3d0fdf2e24fdea6d971ce73f4fbf2;pid=1062862;pidfdid=1063162;comm=systemd-nspawn;container=foobar", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    const cs::Command &c = cmd->context_signal;
    cs::ContextType t;
    ZStr v;
    uint64_t n;
    ASSERT_TRUE(c.action == cs::Command::Action::start);
    ASSERT_TRUE(c.id.eql("bed86fab93af4328bbed0a1224af6d40"));
    ASSERT_TRUE(c.readType(&t) && t == cs::ContextType::container);
    ASSERT_TRUE(c.readString(cs::Field::user, &v) && v.eql("lennart"));
    ASSERT_TRUE(c.readString(cs::Field::hostname, &v) && v.eql("zeta"));
    ASSERT_TRUE(c.readString(cs::Field::comm, &v) && v.eql("systemd-nspawn"));
    ASSERT_TRUE(c.readString(cs::Field::container, &v) && v.eql("foobar"));
    ASSERT_TRUE(c.readU64(cs::Field::pid, &n) && n == 1062862);
}

TEST(context_signal, OSC_3008_spec_example_context_end) {
    /* From the spec: context end */
    Parser p;
    Command *cmd = feed_end(p, "3008;end=bed86fab93af4328bbed0a1224af6d40", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::end);
    ASSERT_TRUE(cmd->context_signal.id.eql("bed86fab93af4328bbed0a1224af6d40"));
}

TEST(context_signal, OSC_3008_cwd_and_cmdline_fields) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=myctx;type=command;cwd=/home/user;cmdline=ls -la", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ZStr v;
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::cwd, &v) && v.eql("/home/user"));
    ASSERT_TRUE(cmd->context_signal.readString(cs::Field::cmdline, &v) && v.eql("ls -la"));
}

TEST(context_signal, OSC_3008_start_command_with_no_fields) {
    Parser p;
    Command *cmd = feed_end(p, "3008;start=simpleid", false, 0);
    ASSERT_TRUE(cmd && cmd->key == Command::Key::context_signal);
    ASSERT_TRUE(cmd->context_signal.action == cs::Command::Action::start);
    ASSERT_TRUE(cmd->context_signal.id.eql("simpleid"));
    cs::ContextType t;
    ZStr v;
    cs::ExitStatus e;
    ASSERT_FALSE(cmd->context_signal.readType(&t));
    ASSERT_FALSE(cmd->context_signal.readString(cs::Field::user, &v));
    ASSERT_FALSE(cmd->context_signal.readExit(&e));
}

/* iterm2.zig */

TEST(iterm2, OSC_1337_test_valid_unimplemented_key_with_no_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;SetBadgeFormat", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_valid_unimplemented_key_with_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;SetBadgeFormat=", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_valid_unimplemented_key_with_non_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;SetBadgeFormat=abc123", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_valid_key_with_lower_case_and_with_no_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;setbadgeformat", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_valid_key_with_lower_case_and_with_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;setbadgeformat=", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_valid_key_with_lower_case_and_with_non_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;setbadgeformat=abc123", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_invalid_key_with_no_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;BobrKurwa", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_invalid_key_with_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;BobrKurwa=", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_invalid_key_with_non_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;BobrKurwa=abc123", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_Copy_with_no_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;Copy", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_Copy_with_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;Copy=", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_Copy_with_only_prefix_colon) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;Copy=:", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_Copy_with_question_mark) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;Copy=:?", true, '\x1b') == nullptr);
}

/* "OSC: 1337: test Copy with non-empty value that is invalid base64" is
 * skipped upstream (error.SkipZigTest): for performance reasons, base64 is
 * not checked right now. */

TEST(iterm2, OSC_1337_test_Copy_with_non_empty_value_that_is_valid_base64_but_not_prefixed_with_a_colon) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;Copy=YWJjMTIz", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_Copy_with_non_empty_value_that_is_valid_base64) {
    Parser p(true);
    Command *cmd = feed_end(p, "1337;Copy=:YWJjMTIz", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::clipboard_contents);
    ASSERT_TRUE(cmd->clipboard_contents.kind == 'c');
    ASSERT_TRUE(cmd->clipboard_contents.data.eql("YWJjMTIz"));
}

TEST(iterm2, OSC_1337_test_CurrentDir_with_no_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;CurrentDir", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_CurrentDir_with_empty_value) {
    Parser p(true);
    ASSERT_TRUE(feed_end(p, "1337;CurrentDir=", true, '\x1b') == nullptr);
}

TEST(iterm2, OSC_1337_test_CurrentDir_with_non_empty_value) {
    Parser p(true);
    Command *cmd = feed_end(p, "1337;CurrentDir=abc123", true, '\x1b');
    ASSERT_TRUE(cmd && cmd->key == Command::Key::report_pwd);
    ASSERT_TRUE(cmd->report_pwd.value.eql("abc123"));
}
