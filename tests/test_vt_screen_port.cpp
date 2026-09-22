/* Transliterated from the test blocks in Ghostty src/terminal/Screen.zig
 * and Selection.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Upstream's tests: same inputs, same assertions, same names. Built with
 * WISP_IS_TEST and WISP_SLOW_RUNTIME_SAFETY, as upstream's tests run.
 */

#include <string.h>
#include <string>

#include "test_helpers.h"
#include "../vt/formatter.hpp"

using namespace wisp;
using namespace wisp::vt;

typedef PageList::Pin Pin;
typedef page::Page Page;
typedef point::Point Point;

static zigstd::Allocator talloc() { return zigstd::testing_allocator(); }

struct ScreenHolder {
    Screen s;
    bool ok;
    ScreenHolder(const Screen::Options &o) { ok = Screen::init(talloc(), o, &s); }
    ~ScreenHolder() {
        if (ok) s.deinit();
    }
};

TEST(screen, Screen_reset_cursor_pin_is_not_garbage) {
    ScreenHolder h(Screen::Options(80, 24, (size_t)1000));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    ASSERT_TRUE(s.testWriteString("hello, world"));
    s.reset();
    ASSERT_TRUE(!s.cursor.page_pin->garbage);
}

TEST(screen, Screen_read_and_write) {
    ScreenHolder h(Screen::Options(80, 24, (size_t)1000));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    ASSERT_TRUE(s.cursor.style_id == 0);
    ASSERT_TRUE(s.testWriteString("hello, world"));
    ASSERT_TRUE(s.dumpStringAlloc(Point::screen()) == "hello, world");
}

TEST(screen, Screen_read_and_write_newline) {
    ScreenHolder h(Screen::Options(80, 24, (size_t)1000));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    ASSERT_TRUE(s.testWriteString("hello\nworld"));
    ASSERT_TRUE(s.dumpStringAlloc(Point::screen()) == "hello\nworld");
}

TEST(screen, Screen_read_and_write_scrollback) {
    ScreenHolder h(Screen::Options(80, 2, (size_t)1000));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    ASSERT_TRUE(s.testWriteString("hello\nworld\ntest"));
    ASSERT_TRUE(s.dumpStringAlloc(Point::screen()) == "hello\nworld\ntest");
    ASSERT_TRUE(s.dumpStringAlloc(Point::active()) == "world\ntest");
}

TEST(screen, Screen_read_and_write_no_scrollback_small) {
    ScreenHolder h(Screen::Options(80, 2, (size_t)0));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    ASSERT_TRUE(s.testWriteString("hello\nworld\ntest"));
    ASSERT_TRUE(s.dumpStringAlloc(Point::screen()) == "world\ntest");
    ASSERT_TRUE(s.dumpStringAlloc(Point::active()) == "world\ntest");
}

TEST(screen, Screen_read_and_write_no_scrollback_large) {
    ScreenHolder h(Screen::Options(80, 2, (size_t)0));
    ASSERT_TRUE(h.ok);
    Screen &s = h.s;
    for (int i = 0; i < 1000; i++) {
        char buf[128];
        snprintf(buf, sizeof buf, "%d\n", i);
        ASSERT_TRUE(s.testWriteString(buf));
    }
    ASSERT_TRUE(s.testWriteString("1000"));
    ASSERT_TRUE(s.dumpStringAlloc(Point::screen()) == "999\n1000");
}

/* @@TESTS@@ */

/* Wisp: std.testing.allocator's leak check. Runs last (registration order). */
TEST(screen, zz_Wisp_no_leaks) { ASSERT_TRUE(zigstd::testing_state().live == 0); }
