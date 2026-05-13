#include "test_helpers.h"
#include "../src/terminal/layout.h"

TEST(Layout, RecomputesGridForDifferentDpiScales) {
    TerminalGridLayout base = terminal_compute_grid_layout(800, 600, 8.0f, 16.0f, 4, 4, 32);
    TerminalGridLayout zoom = terminal_compute_grid_layout(800, 600, 12.0f, 24.0f, 4, 4, 32);
    ASSERT_TRUE(base.cols > zoom.cols);
    ASSERT_TRUE(base.rows > zoom.rows);
    ASSERT_TRUE(base.cols >= 80 || base.rows >= 24);
}

TEST(Layout, MessageVisibilityRespondsToShrinkAndExpand) {
    TerminalGridLayout expanded = terminal_compute_grid_layout(800, 600, 8.0f, 16.0f, 4, 4, 32);
    TerminalGridLayout compressed = terminal_compute_grid_layout(180, 120, 8.0f, 16.0f, 4, 4, 32);
    ASSERT_TRUE(terminal_message_fully_visible(&expanded, 2, true));
    ASSERT_FALSE(terminal_message_fully_visible(&compressed, 8, true));
}

TEST(Layout, PixelToCellClampsInsideViewport) {
    TerminalGridLayout layout = terminal_compute_grid_layout(400, 200, 10.0f, 20.0f, 4, 4, 32);
    int col = -1, row = -1;
    terminal_pixel_to_cell(&layout, -100, -100, 10.0f, 20.0f, 4, 4, 32, &col, &row);
    ASSERT_EQ(col, 0);
    ASSERT_EQ(row, 0);
    terminal_pixel_to_cell(&layout, 9999, 9999, 10.0f, 20.0f, 4, 4, 32, &col, &row);
    ASSERT_EQ(col, layout.cols - 1);
    ASSERT_EQ(row, layout.rows - 1);
}
