#include "test_helpers.h"
#include "../src/terminal/vt_parser.h"
#include <string.h>

static char g_title[256];
static void on_title(const char *title, void *ud) {
    (void)ud;
    strncpy(g_title, title, sizeof(g_title) - 1);
    g_title[sizeof(g_title) - 1] = '\0';
}

TEST(VtParser, ParsesTextAndSgrAttributes) {
    ScreenBuffer sb; VtParser vt;
    screen_init(&sb, 20, 4, 8);
    vt_parser_init(&vt, &sb);
    vt_parser_feed(&vt, "A\x1b[31;44mB\x1b[0mC", 15);

    ASSERT_EQ(screen_cell_at(&sb, 0, 0)->ch, 'A');
    ASSERT_EQ(screen_cell_at(&sb, 1, 0)->ch, 'B');
    ASSERT_EQ(screen_cell_at(&sb, 1, 0)->attr.fg_idx, 1);
    ASSERT_EQ(screen_cell_at(&sb, 1, 0)->attr.bg_idx, 4);
    ASSERT_EQ(screen_cell_at(&sb, 2, 0)->ch, 'C');
    ASSERT_EQ(sb.current_attr.fg_idx, 7);
    ASSERT_EQ(sb.current_attr.bg_idx, 0);
    screen_free(&sb);
}

TEST(VtParser, SgrDoesNotAdvanceCursorInColoredPrompt) {
    ScreenBuffer sb; VtParser vt;
    screen_init(&sb, 40, 4, 8);
    vt_parser_init(&vt, &sb);

    const char *prompt = "\x1b[1;32muser\x1b[0m\x1b[2m@\x1b[0m\x1b[1;34mhost\x1b[0m ";
    vt_parser_feed(&vt, prompt, (int)strlen(prompt));

    ASSERT_EQ(sb.cursor_x, 10);
    ASSERT_EQ(screen_cell_at(&sb, 0, 0)->ch, 'u');
    ASSERT_EQ(screen_cell_at(&sb, 3, 0)->ch, 'r');
    ASSERT_EQ(screen_cell_at(&sb, 4, 0)->ch, '@');
    ASSERT_EQ(screen_cell_at(&sb, 5, 0)->ch, 'h');
    ASSERT_EQ(screen_cell_at(&sb, 9, 0)->ch, ' ');
    screen_free(&sb);
}

TEST(VtParser, SupportsAlternateScreenAndTitle) {
    ScreenBuffer sb; VtParser vt;
    memset(g_title, 0, sizeof(g_title));
    screen_init(&sb, 20, 4, 8);
    vt_parser_init(&vt, &sb);
    vt.on_title = on_title;

    vt_parser_feed(&vt, "\x1b]2;My title\x07", 13);
    ASSERT_STR_EQ(sb.title, "My title");
    ASSERT_STR_EQ(g_title, "My title");

    vt_parser_feed(&vt, "\x1b[?1049h", 8);
    ASSERT_TRUE(sb.alt_screen_active);
    vt_parser_feed(&vt, "\x1b[?1049l", 8);
    ASSERT_FALSE(sb.alt_screen_active);
    screen_free(&sb);
}
