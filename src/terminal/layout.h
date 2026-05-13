#pragma once
#ifndef WSH_TERMINAL_LAYOUT_H
#define WSH_TERMINAL_LAYOUT_H

#include "wsh_bool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int usable_width_px;
    int usable_height_px;
    int cols;
    int rows;
} TerminalGridLayout;

TerminalGridLayout terminal_compute_grid_layout(int width_px, int height_px,
                                                float cell_w, float cell_h,
                                                int padding_x, int padding_y,
                                                int tab_bar_height);

void terminal_pixel_to_cell(const TerminalGridLayout *layout,
                            int px, int py,
                            float cell_w, float cell_h,
                            int padding_x, int padding_y,
                            int tab_bar_height,
                            int *col, int *row);

bool terminal_message_fully_visible(const TerminalGridLayout *layout,
                                    int line_count,
                                    bool include_prompt_line);

#ifdef __cplusplus
}
#endif

#endif
