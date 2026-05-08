#include "layout.h"

TerminalGridLayout terminal_compute_grid_layout(int width_px, int height_px,
                                                float cell_w, float cell_h,
                                                int padding_x, int padding_y,
                                                int tab_bar_height) {
    TerminalGridLayout out = {0};
    if (width_px < 1) width_px = 1;
    if (height_px < 1) height_px = 1;

    out.usable_width_px = width_px - 2 * padding_x;
    out.usable_height_px = height_px - 2 * padding_y - tab_bar_height;
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
    int ox = padding_x;
    int oy = padding_y + tab_bar_height;
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
