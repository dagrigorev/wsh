#include "layout.h"
#include <stddef.h>

TerminalGridLayout terminal_compute_grid_layout(int width_px, int height_px,
                                                float cell_w, float cell_h,
                                                int padding_x, int padding_y,
                                                int tab_bar_height) {
    return terminal_compute_grid_layout_ex(width_px, height_px, cell_w, cell_h,
                                           padding_x, padding_y, 0, tab_bar_height, 0, 0);
}

TerminalGridLayout terminal_compute_grid_layout_ex(int width_px, int height_px,
                                                   float cell_w, float cell_h,
                                                   int padding_x, int padding_y,
                                                   int left_reserved_px,
                                                   int top_reserved_px,
                                                   int right_reserved_px,
                                                   int bottom_reserved_px) {
    TerminalGridLayout out = {0};
    if (width_px < 1) width_px = 1;
    if (height_px < 1) height_px = 1;
    if (left_reserved_px < 0) left_reserved_px = 0;
    if (top_reserved_px < 0) top_reserved_px = 0;
    if (right_reserved_px < 0) right_reserved_px = 0;
    if (bottom_reserved_px < 0) bottom_reserved_px = 0;

    out.origin_x_px = left_reserved_px + padding_x;
    out.origin_y_px = top_reserved_px + padding_y;
    out.usable_width_px = width_px - left_reserved_px - right_reserved_px - 2 * padding_x;
    out.usable_height_px = height_px - top_reserved_px - bottom_reserved_px - 2 * padding_y;
    if (out.usable_width_px < 0) out.usable_width_px = 0;
    if (out.usable_height_px < 0) out.usable_height_px = 0;

    out.cols = cell_w > 0 ? (int)(out.usable_width_px / cell_w) : 80;
    out.rows = cell_h > 0 ? (int)(out.usable_height_px / cell_h) : 24;
    if (out.cols < 1) out.cols = 1;
    if (out.rows < 1) out.rows = 1;
    return out;
}

void terminal_pixel_to_cell(const TerminalGridLayout *layout,
                            int px, int py,
                            float cell_w, float cell_h,
                            int padding_x, int padding_y,
                            int tab_bar_height,
                            int *col, int *row) {
    TerminalGridLayout local = {0};
    if (layout) local = *layout;
    local.origin_x_px = padding_x;
    local.origin_y_px = padding_y + tab_bar_height;
    terminal_pixel_to_cell_ex(layout ? &local : NULL, px, py, cell_w, cell_h,
                              padding_x, padding_y, col, row);
}

void terminal_pixel_to_cell_ex(const TerminalGridLayout *layout,
                               int px, int py,
                               float cell_w, float cell_h,
                               int padding_x, int padding_y,
                               int *col, int *row) {
    (void)padding_x;
    (void)padding_y;
    int ox = layout ? layout->origin_x_px : 0;
    int oy = layout ? layout->origin_y_px : 0;
    int c = (cell_w > 0) ? (int)((px - ox) / cell_w) : -1;
    int r = (cell_h > 0) ? (int)((py - oy) / cell_h) : -1;

    if (c < 0) c = 0;
    if (r < 0) r = 0;
    if (layout) {
        if (c >= layout->cols) c = layout->cols - 1;
        if (r >= layout->rows) r = layout->rows - 1;
    }

    if (col) *col = c;
    if (row) *row = r;
}

bool terminal_message_fully_visible(const TerminalGridLayout *layout,
                                    int line_count,
                                    bool include_prompt_line) {
    if (!layout) return false;
    if (line_count < 0) line_count = 0;
    int needed = line_count + (include_prompt_line ? 1 : 0);
    return layout->rows >= needed;
}
