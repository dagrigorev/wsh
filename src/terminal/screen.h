#pragma once
#ifndef WSH_SCREEN_H
#define WSH_SCREEN_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

/* ─── Cell Attribute ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  fg_idx;      /* 0-255 ANSI palette index; 0xFF = use fg_rgb */
    uint8_t  bg_idx;      /* 0-255 ANSI palette index; 0xFF = use bg_rgb */
    uint32_t fg_rgb;      /* Truecolor when fg_idx == 0xFF */
    uint32_t bg_rgb;      /* Truecolor when bg_idx == 0xFF */
    uint8_t  bold     : 1;
    uint8_t  italic   : 1;
    uint8_t  underline: 1;
    uint8_t  blink    : 1;
    uint8_t  reverse  : 1;
    uint8_t  dim      : 1;
    uint8_t  strikethrough : 1;
    uint8_t  _pad     : 1;
} CellAttr;

/* ─── Screen Cell ────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t ch;          /* Unicode codepoint (0 = empty / space) */
    CellAttr attr;
    uint8_t  wide     : 1; /* CJK double-width lead cell */
    uint8_t  wide_cont: 1; /* CJK double-width continuation cell */
    uint8_t  dirty    : 1; /* Needs repaint */
    uint8_t  _pad     : 5;
} ScreenCell;

/* Default "blank" cell */
static inline ScreenCell screen_cell_blank(void) {
    ScreenCell c = {0};
    c.ch           = ' ';
    c.attr.fg_idx  = 7;   /* Default white */
    c.attr.bg_idx  = 0;   /* Default black */
    c.dirty        = 1;
    return c;
}

/* ─── Screen Buffer ──────────────────────────────────────────────────────── */

typedef struct {
    /* Primary and alternate screen cell grids */
    ScreenCell *cells;          /* [rows * cols] — primary screen */
    ScreenCell *alt_cells;      /* [rows * cols] — alternate (vim, less, etc.) */
    int         cols, rows;

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

    /* Window title (set via OSC 0) */
    char        title[256];
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

#endif /* WSH_SCREEN_H */
