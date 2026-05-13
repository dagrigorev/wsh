#include "test_helpers.h"
#include "../src/terminal/screen.h"

TEST(Screen, StoresCharactersAndWraps) {
    ScreenBuffer sb;
    screen_init(&sb, 3, 2, 8);
    screen_put_char(&sb, 'A', NULL);
    screen_put_char(&sb, 'B', NULL);
    screen_put_char(&sb, 'C', NULL);
    screen_put_char(&sb, 'D', NULL);

    ASSERT_EQ(screen_cell_at(&sb, 0, 0)->ch, 'A');
    ASSERT_EQ(screen_cell_at(&sb, 2, 0)->ch, 'C');
    ASSERT_EQ(screen_cell_at(&sb, 0, 1)->ch, 'D');
    screen_free(&sb);
}

TEST(Screen, TracksWideCharactersAcrossTwoCells) {
    ScreenBuffer sb;
    screen_init(&sb, 4, 2, 8);
    screen_put_char(&sb, 0x4E16, NULL); /* 世 */
    ASSERT_TRUE(screen_cell_at(&sb, 0, 0)->wide);
    ASSERT_TRUE(screen_cell_at(&sb, 1, 0)->wide_cont);
    ASSERT_EQ(sb.cursor_x, 2);
    screen_free(&sb);
}

TEST(Screen, PreservesVisibleTextAcrossShrinkAndExpand) {
    ScreenBuffer sb;
    screen_init(&sb, 10, 4, 8);
    const char *msg1 = " Wsh v1.0";
    const char *msg2 = "help reload";
    for (const char *p = msg1; *p; ++p) screen_put_char(&sb, (unsigned char)*p, NULL);
    screen_newline(&sb); screen_carriage_return(&sb);
    for (const char *p = msg2; *p; ++p) screen_put_char(&sb, (unsigned char)*p, NULL);

    screen_resize(&sb, 6, 2);
    ASSERT_EQ(screen_cell_at(&sb, 0, 0)->ch, ' ');
    ASSERT_EQ(screen_cell_at(&sb, 1, 0)->ch, 'W');
    ASSERT_EQ(screen_cell_at(&sb, 0, 1)->ch, 'h');

    screen_resize(&sb, 12, 4);
    ASSERT_EQ(screen_cell_at(&sb, 1, 0)->ch, 'W');
    ASSERT_EQ(screen_cell_at(&sb, 0, 1)->ch, 'h');
    screen_free(&sb);
}

TEST(Screen, ScrollbackRetainsScrolledOffLines) {
    ScreenBuffer sb;
    screen_init(&sb, 4, 2, 8);
    for (int i = 0; i < 4; ++i) screen_put_char(&sb, 'A' + i, NULL);
    screen_newline(&sb); screen_carriage_return(&sb);
    for (int i = 0; i < 4; ++i) screen_put_char(&sb, 'E' + i, NULL);
    screen_newline(&sb); screen_carriage_return(&sb);
    for (int i = 0; i < 4; ++i) screen_put_char(&sb, 'I' + i, NULL);

    ASSERT_TRUE(sb.scrollback_count >= 1);
    ScreenCell *old = screen_scrollback_line(&sb, sb.scrollback_count - 1, 0);
    ASSERT_NOT_NULL(old);
    ASSERT_EQ(old->ch, 'A');
    screen_free(&sb);
}

TEST(Screen, ViewportShowsMixedScrollbackAndLiveRows) {
    ScreenBuffer sb;
    screen_init(&sb, 4, 2, 8);

    const char *lines[] = {"AAAA", "BBBB", "CCCC"};
    for (int l = 0; l < 3; ++l) {
        for (int i = 0; i < 4; ++i) screen_put_char(&sb, (unsigned char)lines[l][i], NULL);
        if (l != 2) { screen_newline(&sb); screen_carriage_return(&sb); }
    }

    ASSERT_EQ(screen_visible_cell(&sb, 0, 0)->ch, 'B');
    ASSERT_EQ(screen_visible_cell(&sb, 1, 0)->ch, 'C');

    screen_scroll_viewport(&sb, 1);
    ASSERT_EQ(sb.viewport_offset, 1);
    ASSERT_EQ(screen_visible_cell(&sb, 0, 0)->ch, 'A');
    ASSERT_EQ(screen_visible_cell(&sb, 1, 0)->ch, 'B');

    screen_free(&sb);
}

TEST(Screen, OutputResetsViewportToLiveBottom) {
    ScreenBuffer sb;
    screen_init(&sb, 4, 2, 8);

    const char *lines[] = {"AAAA", "BBBB", "CCCC"};
    for (int l = 0; l < 3; ++l) {
        for (int i = 0; i < 4; ++i) screen_put_char(&sb, (unsigned char)lines[l][i], NULL);
        if (l != 2) { screen_newline(&sb); screen_carriage_return(&sb); }
    }

    screen_scroll_viewport(&sb, 1);
    ASSERT_EQ(sb.viewport_offset, 1);
    screen_put_char(&sb, 'D', NULL);
    ASSERT_EQ(sb.viewport_offset, 0);

    screen_free(&sb);
}
