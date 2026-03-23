#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "screen.h"
#include "util.h"

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static void fill_cells(ScreenCell *cells, int count, const CellAttr *attr) {
    ScreenCell blank = screen_cell_blank();
    if (attr) blank.attr = *attr;
    blank.dirty = 1;
    for (int i = 0; i < count; i++) cells[i] = blank;
}

static ScreenCell *active_grid(ScreenBuffer *sb) {
    return sb->alt_screen_active ? sb->alt_cells : sb->cells;
}

/* ─── Init / Free ────────────────────────────────────────────────────────── */

void screen_init(ScreenBuffer *sb, int cols, int rows, int scrollback_lines) {
    memset(sb, 0, sizeof(*sb));
    sb->cols           = cols;
    sb->rows           = rows;
    sb->cursor_visible = true;
    sb->auto_wrap      = true;
    sb->scroll_top     = 0;
    sb->scroll_bot     = rows - 1;

    /* Default attributes */
    sb->current_attr.fg_idx = 7;
    sb->current_attr.bg_idx = 0;

    int total = cols * rows;
    sb->cells     = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                            (size_t)total * sizeof(ScreenCell));
    sb->alt_cells = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                            (size_t)total * sizeof(ScreenCell));
    fill_cells(sb->cells,     total, NULL);
    fill_cells(sb->alt_cells, total, NULL);

    /* Scrollback */
    sb->scrollback_capacity = scrollback_lines > 0 ? scrollback_lines : 10000;
    sb->scrollback = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                     (size_t)(sb->scrollback_capacity * cols) * sizeof(ScreenCell));
    sb->scrollback_head  = 0;
    sb->scrollback_count = 0;
}

void screen_free(ScreenBuffer *sb) {
    HeapFree(GetProcessHeap(), 0, sb->cells);
    HeapFree(GetProcessHeap(), 0, sb->alt_cells);
    HeapFree(GetProcessHeap(), 0, sb->scrollback);
    memset(sb, 0, sizeof(*sb));
}

void screen_resize(ScreenBuffer *sb, int new_cols, int new_rows) {
    if (new_cols == sb->cols && new_rows == sb->rows) return;

    int total = new_cols * new_rows;
    ScreenCell *nc  = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                              (size_t)total * sizeof(ScreenCell));
    ScreenCell *nca = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                              (size_t)total * sizeof(ScreenCell));
    fill_cells(nc,  total, NULL);
    fill_cells(nca, total, NULL);

    /* Copy what we can */
    int copy_rows = new_rows < sb->rows ? new_rows : sb->rows;
    int copy_cols = new_cols < sb->cols ? new_cols : sb->cols;
    for (int r = 0; r < copy_rows; r++) {
        for (int c = 0; c < copy_cols; c++) {
            nc[r * new_cols + c]  = sb->cells[r * sb->cols + c];
            nca[r * new_cols + c] = sb->alt_cells[r * sb->cols + c];
        }
    }

    HeapFree(GetProcessHeap(), 0, sb->cells);
    HeapFree(GetProcessHeap(), 0, sb->alt_cells);
    sb->cells     = nc;
    sb->alt_cells = nca;
    sb->cols      = new_cols;
    sb->rows      = new_rows;

    /* Clamp cursor */
    if (sb->cursor_x >= new_cols) sb->cursor_x = new_cols - 1;
    if (sb->cursor_y >= new_rows) sb->cursor_y = new_rows - 1;

    /* Reset scroll region */
    sb->scroll_top = 0;
    sb->scroll_bot = new_rows - 1;

    /* Resize scrollback row width */
    ScreenCell *nsb = (ScreenCell *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                      (size_t)(sb->scrollback_capacity * new_cols) * sizeof(ScreenCell));
    if (nsb) {
        fill_cells(nsb, sb->scrollback_capacity * new_cols, NULL);
        HeapFree(GetProcessHeap(), 0, sb->scrollback);
        sb->scrollback       = nsb;
        sb->scrollback_head  = 0;
        sb->scrollback_count = 0;
    }
}

/* ─── Cell Access ────────────────────────────────────────────────────────── */

ScreenCell *screen_cell_at(ScreenBuffer *sb, int col, int row) {
    if (col < 0 || col >= sb->cols || row < 0 || row >= sb->rows) return NULL;
    return active_grid(sb) + row * sb->cols + col;
}

/* ─── Cursor Movement ────────────────────────────────────────────────────── */

void screen_set_cursor(ScreenBuffer *sb, int x, int y) {
    int top = sb->origin_mode ? sb->scroll_top : 0;
    int bot = sb->origin_mode ? sb->scroll_bot : sb->rows - 1;
    sb->cursor_x = x < 0 ? 0 : (x >= sb->cols ? sb->cols - 1 : x);
    sb->cursor_y = y < top ? top : (y > bot ? bot : y);
}

void screen_move_cursor(ScreenBuffer *sb, int dx, int dy) {
    screen_set_cursor(sb, sb->cursor_x + dx, sb->cursor_y + dy);
}

void screen_save_cursor(ScreenBuffer *sb) {
    sb->saved_x    = sb->cursor_x;
    sb->saved_y    = sb->cursor_y;
    sb->saved_attr = sb->current_attr;
    sb->saved_origin_mode = sb->origin_mode;
}

void screen_restore_cursor(ScreenBuffer *sb) {
    sb->cursor_x      = sb->saved_x;
    sb->cursor_y      = sb->saved_y;
    sb->current_attr  = sb->saved_attr;
    sb->origin_mode   = sb->saved_origin_mode;
    if (sb->cursor_x >= sb->cols) sb->cursor_x = sb->cols - 1;
    if (sb->cursor_y >= sb->rows) sb->cursor_y = sb->rows - 1;
}

/* ─── Scrollback Storage ─────────────────────────────────────────────────── */

static void push_line_to_scrollback(ScreenBuffer *sb, ScreenCell *line) {
    if (!sb->scrollback || sb->scrollback_capacity == 0) return;
    ScreenCell *dest = sb->scrollback + sb->scrollback_head * sb->cols;
    memcpy(dest, line, (size_t)sb->cols * sizeof(ScreenCell));
    sb->scrollback_head = (sb->scrollback_head + 1) % sb->scrollback_capacity;
    if (sb->scrollback_count < sb->scrollback_capacity) sb->scrollback_count++;
}

/* ─── Scroll Region Scroll ───────────────────────────────────────────────── */

void screen_scroll_up(ScreenBuffer *sb, int top, int bot, int n) {
    /* Lines scroll upward: lines [top+n..bot] move to [top..bot-n]; bottom n cleared */
    ScreenCell *grid = active_grid(sb);
    if (n <= 0 || top > bot) return;
    if (n > bot - top + 1) n = bot - top + 1;

    /* Save scrolled-off lines to scrollback (primary only) */
    if (!sb->alt_screen_active) {
        for (int i = 0; i < n; i++) {
            push_line_to_scrollback(sb, grid + (top + i) * sb->cols);
        }
    }

    int move = bot - top + 1 - n;
    if (move > 0) {
        memmove(grid + top * sb->cols,
                grid + (top + n) * sb->cols,
                (size_t)(move * sb->cols) * sizeof(ScreenCell));
    }
    /* Clear new blank lines at bottom */
    for (int r = bot - n + 1; r <= bot; r++) {
        fill_cells(grid + r * sb->cols, sb->cols, &sb->current_attr);
    }
}

void screen_scroll_down(ScreenBuffer *sb, int top, int bot, int n) {
    /* Lines scroll downward: lines [top..bot-n] move to [top+n..bot]; top n cleared */
    ScreenCell *grid = active_grid(sb);
    if (n <= 0 || top > bot) return;
    if (n > bot - top + 1) n = bot - top + 1;

    int move = bot - top + 1 - n;
    if (move > 0) {
        memmove(grid + (top + n) * sb->cols,
                grid + top * sb->cols,
                (size_t)(move * sb->cols) * sizeof(ScreenCell));
    }
    for (int r = top; r < top + n; r++) {
        fill_cells(grid + r * sb->cols, sb->cols, &sb->current_attr);
    }
}

void screen_scroll_viewport(ScreenBuffer *sb, int delta) {
    sb->viewport_offset += delta;
    if (sb->viewport_offset < 0) sb->viewport_offset = 0;
    if (sb->viewport_offset > sb->scrollback_count)
        sb->viewport_offset = sb->scrollback_count;
    screen_mark_dirty_all(sb);
}

/* ─── Character Output ───────────────────────────────────────────────────── */

void screen_newline(ScreenBuffer *sb) {
    if (sb->cursor_y < sb->scroll_bot) {
        sb->cursor_y++;
    } else {
        screen_scroll_up(sb, sb->scroll_top, sb->scroll_bot, 1);
    }
}

void screen_carriage_return(ScreenBuffer *sb) {
    sb->cursor_x = 0;
}

void screen_put_char(ScreenBuffer *sb, uint32_t ch, const CellAttr *attr) {
    /* Detect wide (CJK) characters — simplified: U+1100..U+115F, U+2E80..U+A4CF, etc. */
    bool wide = (ch >= 0x1100 && ch <= 0x115F) ||
                (ch >= 0x2E80 && ch <= 0xA4CF) ||
                (ch >= 0xAC00 && ch <= 0xD7AF) ||
                (ch >= 0xF900 && ch <= 0xFAFF) ||
                (ch >= 0xFE10 && ch <= 0xFE6F) ||
                (ch >= 0xFF00 && ch <= 0xFF60) ||
                (ch >= 0x1F300 && ch <= 0x1FAFF);

    /* Auto-wrap */
    if (sb->cursor_x >= sb->cols) {
        if (sb->auto_wrap) {
            sb->cursor_x = 0;
            screen_newline(sb);
        } else {
            sb->cursor_x = sb->cols - 1;
        }
    }

    /* Wide char that won't fit on this line */
    if (wide && sb->cursor_x == sb->cols - 1) {
        /* Fill current cell with space, wrap */
        ScreenCell *c = screen_cell_at(sb, sb->cursor_x, sb->cursor_y);
        if (c) { *c = screen_cell_blank(); c->dirty = 1; }
        sb->cursor_x = 0;
        screen_newline(sb);
    }

    ScreenCell *c = screen_cell_at(sb, sb->cursor_x, sb->cursor_y);
    if (!c) return;

    c->ch       = ch;
    c->attr     = attr ? *attr : sb->current_attr;
    c->wide     = wide ? 1 : 0;
    c->wide_cont= 0;
    c->dirty    = 1;
    sb->cursor_x++;

    if (wide && sb->cursor_x < sb->cols) {
        ScreenCell *c2 = screen_cell_at(sb, sb->cursor_x, sb->cursor_y);
        if (c2) {
            c2->ch        = ' ';
            c2->attr      = c->attr;
            c2->wide      = 0;
            c2->wide_cont = 1;
            c2->dirty     = 1;
        }
        sb->cursor_x++;
    }
}

/* ─── Erase ──────────────────────────────────────────────────────────────── */

void screen_erase_line(ScreenBuffer *sb, int mode) {
    ScreenCell *grid = active_grid(sb);
    int row = sb->cursor_y;
    ScreenCell blank = screen_cell_blank();
    blank.attr = sb->current_attr;
    blank.attr.bold = blank.attr.italic = blank.attr.underline = 0;

    int from, to;
    switch (mode) {
        case 0: from = sb->cursor_x; to = sb->cols - 1; break;
        case 1: from = 0;            to = sb->cursor_x;  break;
        case 2: from = 0;            to = sb->cols - 1;  break;
        default: return;
    }
    for (int c = from; c <= to; c++) {
        grid[row * sb->cols + c] = blank;
        grid[row * sb->cols + c].dirty = 1;
    }
}

void screen_erase_display(ScreenBuffer *sb, int mode) {
    ScreenCell *grid = active_grid(sb);
    ScreenCell blank = screen_cell_blank();
    blank.attr = sb->current_attr;

    if (mode == 0) {
        /* From cursor to end */
        screen_erase_line(sb, 0);
        for (int r = sb->cursor_y + 1; r < sb->rows; r++)
            fill_cells(grid + r * sb->cols, sb->cols, &blank.attr);
    } else if (mode == 1) {
        /* From start to cursor */
        for (int r = 0; r < sb->cursor_y; r++)
            fill_cells(grid + r * sb->cols, sb->cols, &blank.attr);
        screen_erase_line(sb, 1);
    } else if (mode == 2 || mode == 3) {
        /* All screen */
        fill_cells(grid, sb->cols * sb->rows, &blank.attr);
        if (mode == 3) {
            /* Also clear scrollback */
            sb->scrollback_head  = 0;
            sb->scrollback_count = 0;
        }
    }
}

void screen_erase_chars(ScreenBuffer *sb, int n) {
    ScreenCell blank = screen_cell_blank();
    blank.attr = sb->current_attr;
    ScreenCell *grid = active_grid(sb);
    for (int i = 0; i < n && sb->cursor_x + i < sb->cols; i++) {
        grid[sb->cursor_y * sb->cols + sb->cursor_x + i] = blank;
        grid[sb->cursor_y * sb->cols + sb->cursor_x + i].dirty = 1;
    }
}

/* ─── Line Ops ───────────────────────────────────────────────────────────── */

void screen_insert_lines(ScreenBuffer *sb, int n) {
    screen_scroll_down(sb, sb->cursor_y, sb->scroll_bot, n);
}

void screen_delete_lines(ScreenBuffer *sb, int n) {
    screen_scroll_up(sb, sb->cursor_y, sb->scroll_bot, n);
}

void screen_insert_chars(ScreenBuffer *sb, int n) {
    ScreenCell *grid = active_grid(sb);
    int row = sb->cursor_y;
    int from = sb->cursor_x;
    int move = sb->cols - from - n;
    if (move > 0) {
        memmove(grid + row * sb->cols + from + n,
                grid + row * sb->cols + from,
                (size_t)move * sizeof(ScreenCell));
    }
    ScreenCell blank = screen_cell_blank();
    blank.attr = sb->current_attr;
    for (int i = 0; i < n && from + i < sb->cols; i++) {
        grid[row * sb->cols + from + i] = blank;
        grid[row * sb->cols + from + i].dirty = 1;
    }
}

void screen_delete_chars(ScreenBuffer *sb, int n) {
    ScreenCell *grid = active_grid(sb);
    int row = sb->cursor_y;
    int from = sb->cursor_x;
    int move = sb->cols - from - n;
    if (move > 0) {
        memmove(grid + row * sb->cols + from,
                grid + row * sb->cols + from + n,
                (size_t)move * sizeof(ScreenCell));
    }
    ScreenCell blank = screen_cell_blank();
    blank.attr = sb->current_attr;
    for (int i = sb->cols - n; i < sb->cols; i++) {
        if (i >= from) { grid[row * sb->cols + i] = blank; grid[row * sb->cols + i].dirty = 1; }
    }
}

/* ─── Alternate Screen ───────────────────────────────────────────────────── */

void screen_enter_alt(ScreenBuffer *sb) {
    if (sb->alt_screen_active) return;
    sb->alt_screen_active = true;
    fill_cells(sb->alt_cells, sb->cols * sb->rows, NULL);
    screen_mark_dirty_all(sb);
}

void screen_leave_alt(ScreenBuffer *sb) {
    if (!sb->alt_screen_active) return;
    sb->alt_screen_active = false;
    screen_mark_dirty_all(sb);
}

/* ─── Dirty Marking ──────────────────────────────────────────────────────── */

void screen_mark_dirty_all(ScreenBuffer *sb) {
    ScreenCell *grid = active_grid(sb);
    int total = sb->cols * sb->rows;
    for (int i = 0; i < total; i++) grid[i].dirty = 1;
}

/* ─── Scrollback Access ──────────────────────────────────────────────────── */

ScreenCell *screen_scrollback_line(ScreenBuffer *sb, int line_offset, int col) {
    if (!sb->scrollback || line_offset < 0 || line_offset >= sb->scrollback_count)
        return NULL;
    if (col < 0 || col >= sb->cols) return NULL;
    int idx = (sb->scrollback_head - 1 - line_offset + sb->scrollback_capacity)
              % sb->scrollback_capacity;
    return sb->scrollback + idx * sb->cols + col;
}
