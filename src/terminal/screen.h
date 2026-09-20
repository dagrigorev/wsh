#pragma once
#ifndef WISP_SCREEN_H
#define WISP_SCREEN_H

#include <windows.h>
#include "wisp_bool.h"
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ─── Cell Attribute ─────────────────────────────────────────────────────── */

/* CellAttr now lives with the style table, which owns the interning. */
#include "style_table.h"

/* ─── Screen Cell ────────────────────────────────────────────────────────── */

/* A cell holds a style ID rather than its attributes.
 *
 * Attributes used to be stored inline, sixteen bytes of color and flags in
 * every cell, which put a ScreenCell at twenty-four bytes. Most cells on a
 * screen share a handful of styles, so the distinct ones live once in the
 * buffer's StyleTable and a cell keeps a two-byte ID. That takes a cell to
 * eight bytes: for a 200-column buffer with 10,000 lines of scrollback, 46 MB
 * of cells becomes 15 MB.
 *
 * Style ID 0 is the default style, so a zeroed cell is already sensible. */
typedef struct {
    uint32_t ch;          /* Unicode codepoint (0 = empty / space) */
    uint16_t style_id;    /* Index into the buffer's StyleTable; 0 = default */
    uint8_t  wide     : 1; /* CJK double-width lead cell */
    uint8_t  wide_cont: 1; /* CJK double-width continuation cell */
    uint8_t  dirty    : 1; /* Needs repaint */
    uint8_t  _pad     : 5;
} ScreenCell;

/* Default "blank" cell */
static inline ScreenCell screen_cell_blank(void) {
    ScreenCell c = {0};
    c.ch       = ' ';
    c.style_id = 0;   /* default style: white on black */
    c.dirty    = 1;
    return c;
}

/* ─── Screen Buffer ──────────────────────────────────────────────────────── */

typedef struct {
    /* Primary and alternate screen cell grids */
    ScreenCell *cells;          /* [rows * cols] — primary screen */
    ScreenCell *alt_cells;      /* [rows * cols] — alternate (vim, less, etc.) */
    int         cols, rows;

    /* Interned styles referenced by every cell's style_id.
     *
     * Grow-only: styles are never released when a cell is overwritten or
     * scrolled away. Cells are copied in a dozen places — scroll, resize,
     * erase, scrollback push — and a single missed release there would be a
     * use-after-free surfacing as wrong colors much later. Treating this as a
     * cache costs at most a bounded table and needs no bookkeeping in any of
     * those paths. Distinct styles are deduped, so the table tracks how many
     * styles a session actually uses, not how many cells use them. */
    StyleTable *styles;

    /* Cursor */
    int         cursor_x, cursor_y;
    bool        cursor_visible;
    CellAttr    current_attr;   /* Attributes applied to next written char */

    /* Saved cursor (DECSC/DECRC, \e7/\e8) */
    int         saved_x, saved_y;
    CellAttr    saved_attr;
    bool        saved_origin_mode;

    /* Scroll region (1-based in VT, 0-based stored here) */
    int         scroll_top;     /* inclusive */
    int         scroll_bot;     /* inclusive */

    /* Mode flags */
    bool        alt_screen_active;
    bool        origin_mode;     /* DECOM */
    bool        auto_wrap;       /* DECAWM */
    bool        bracketed_paste; /* ?2004 */
    bool        app_cursor_keys; /* ?1 — DECCKM */
    bool        insert_mode;

    /* Scrollback ring buffer (primary screen only) */
    ScreenCell *scrollback;          /* [scrollback_capacity * cols] */
    int         scrollback_capacity; /* number of lines */
    int         scrollback_head;     /* next write index (ring) */
    int         scrollback_count;    /* number of valid lines stored */
    int         viewport_offset;     /* 0 = live bottom; positive = scrolled up */
    bool        scroll_on_output;     /* zsh/terminal-like: output returns viewport to bottom */

    /* Window title (set via OSC 0) */
    char        title[256];

    /* Extra bottom rows: visible when viewport_offset == 0 (for AI suggestions etc.) */
    ScreenCell *extra_bottom;          /* [WISP_OVERSCROLL_LINES * cols] */
} ScreenBuffer;

/* ─── API ────────────────────────────────────────────────────────────────── */

void screen_init(ScreenBuffer *sb, int cols, int rows, int scrollback_lines);
void screen_free(ScreenBuffer *sb);
void screen_resize(ScreenBuffer *sb, int new_cols, int new_rows);

/* Write a character at the current cursor position and advance cursor */
void screen_put_char(ScreenBuffer *sb, uint32_t ch, const CellAttr *attr);

/* Cursor movement */
void screen_set_cursor(ScreenBuffer *sb, int x, int y);
void screen_move_cursor(ScreenBuffer *sb, int dx, int dy);
void screen_save_cursor(ScreenBuffer *sb);
void screen_restore_cursor(ScreenBuffer *sb);
void screen_newline(ScreenBuffer *sb);
void screen_carriage_return(ScreenBuffer *sb);

/* Erase */
void screen_erase_line(ScreenBuffer *sb, int mode);     /* 0=to end, 1=to start, 2=all */
void screen_erase_display(ScreenBuffer *sb, int mode);  /* 0=to end, 1=to start, 2=all, 3=+scrollback */
void screen_erase_chars(ScreenBuffer *sb, int n);       /* ECH */

/* Scrolling */
void screen_scroll_up(ScreenBuffer *sb, int top, int bot, int n);
void screen_scroll_down(ScreenBuffer *sb, int top, int bot, int n);
void screen_scroll_viewport(ScreenBuffer *sb, int delta); /* positive = scroll up */
void screen_page_viewport(ScreenBuffer *sb, int pages);   /* positive = page up */
void screen_set_viewport_offset(ScreenBuffer *sb, int offset);
void screen_reset_viewport(ScreenBuffer *sb);
int  screen_max_viewport_offset(const ScreenBuffer *sb);

/* Line insertion/deletion */
void screen_insert_lines(ScreenBuffer *sb, int n);
void screen_delete_lines(ScreenBuffer *sb, int n);
void screen_insert_chars(ScreenBuffer *sb, int n);
void screen_delete_chars(ScreenBuffer *sb, int n);

/* Alternate screen */
void screen_enter_alt(ScreenBuffer *sb);
void screen_leave_alt(ScreenBuffer *sb);

/* Force-mark all cells dirty (for full repaint) */
void screen_mark_dirty_all(ScreenBuffer *sb);

/* Get a pointer to the cell at (col, row) in the active grid */
ScreenCell *screen_cell_at(ScreenBuffer *sb, int col, int row);

/* Get a cell from scrollback (0 = oldest visible, negative = further back) */
ScreenCell *screen_scrollback_line(ScreenBuffer *sb, int line_offset, int col);
ScreenCell *screen_scrollback_line_from_oldest(ScreenBuffer *sb, int chronological_index, int col);
const ScreenCell *screen_visible_cell(const ScreenBuffer *sb, int viewport_row, int col);

/* Extra bottom row access (rows beyond sb->rows, for AI suggestions) */
#define WISP_OVERSCROLL_LINES 3
ScreenCell *screen_extra_bottom_cell(ScreenBuffer *sb, int line, int col);
void        screen_extra_bottom_clear(ScreenBuffer *sb);
/* Write a character to any valid row (grid 0..rows-1 or extra rows rows..rows+WISP_OVERSCROLL_LINES-1) */
void screen_put_cell_at(ScreenBuffer *sb, int row, int col, uint32_t ch, const CellAttr *attr);


#ifdef __cplusplus
}
#endif

#endif /* WISP_SCREEN_H */
