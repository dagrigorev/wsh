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

TEST(Layout, ReservedChromeReducesGridAndMovesOrigin) {
    TerminalGridLayout plain = terminal_compute_grid_layout_ex(1000, 700, 10.0f, 20.0f, 8, 8, 0, 0, 0, 0);
    TerminalGridLayout chrome = terminal_compute_grid_layout_ex(1000, 700, 10.0f, 20.0f, 8, 8, 196, 112, 0, 24);
    ASSERT_TRUE(chrome.cols < plain.cols);
    ASSERT_TRUE(chrome.rows < plain.rows);
    ASSERT_EQ(chrome.origin_x_px, 204);
    ASSERT_EQ(chrome.origin_y_px, 120);
}

TEST(Layout, PixelToCellHonorsReservedChromeOrigin) {
    TerminalGridLayout layout = terminal_compute_grid_layout_ex(1000, 700, 10.0f, 20.0f, 8, 8, 196, 112, 0, 24);
    int col = -1, row = -1;
    terminal_pixel_to_cell_ex(&layout, layout.origin_x_px + 21, layout.origin_y_px + 41,
                              10.0f, 20.0f, 8, 8, &col, &row);
    ASSERT_EQ(col, 2);
    ASSERT_EQ(row, 2);
}
