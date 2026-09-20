/* Ported from Ghostty src/terminal/Screen.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * A screen: the pages, and the cursor moving over them.
 *
 * PageList holds the text and knows nothing about where anyone is looking at
 * it. A Screen adds the cursor — where the next character goes — and the
 * operations that move it. That sounds small, and the struct is small, but it
 * is where two things that have been kept apart so far finally meet: a
 * position on the screen, which is a column and a row counted from the top of
 * what is displayed, and a position in the text, which is a pin.
 *
 * The cursor is both, and keeping them agreeing is most of this file. The
 * screen coordinates are what a terminal program means by the cursor and what
 * every escape sequence is written in terms of. The pin is what survives the
 * pages changing underneath it. Neither can be dropped: coordinates alone
 * would be wrong after a reflow, and a pin alone cannot answer "what column
 * am I in" without a walk.
 *
 * NOT THE LIVE SCREEN. src/terminal/screen.h is Wisp's working terminal
 * buffer, which is what the application currently runs on. This is the ported
 * one, built on the ported page list, and the two are unrelated for now.
 *
 * PARTIAL PORT. The screen, the cursor and its movement are here. Writing
 * text, erasing, selections, the alternate screen and the saved cursor are
 * not, so the ledger records Screen.zig as `wip`.
 */

#pragma once
#ifndef WISP_TERMINAL_SCREEN_HPP
#define WISP_TERMINAL_SCREEN_HPP

#include <stddef.h>
#include <stdint.h>

#include "page_list.hpp"

namespace wisp {
namespace terminal {

/* ─── the cursor ─────────────────────────────────────────────────────────── */

/* Where the next character goes.
 *
 * x and y are screen coordinates: columns from the left, rows from the top of
 * the active area. They are what escape sequences talk about, and they are
 * only meaningful against the screen as it is now.
 *
 * page_pin is the same place as a pin, tracked so the list maintains it. The
 * two are kept in step by every operation here, and page_row and page_cell
 * are cached from the pin so that writing a character does not cost a lookup.
 *
 * Those cached pointers are the dangerous part. They are addresses inside a
 * page, and every operation that can move rows or replace a page invalidates
 * them. Anything that touches the list must reload them afterwards, which is
 * what screen_cursor_reload is for. */
struct Cursor {
    CellCountInt x;
    CellCountInt y;

    TrackedPin page_pin;

    Row  *page_row;
    Cell *page_cell;

    /* The style new characters are written with. The value is kept rather
     * than an interned ID because an ID belongs to one page's style set, and
     * the cursor crosses pages. Interning happens when a character is
     * actually written. */
    style::Style style;

    /* The cursor is at the right margin and the next character wraps rather
     * than overwriting. DECAWM's doing; kept here because it is a property of
     * where the cursor is, not of the text. */
    bool pending_wrap;

    Cursor()
        : x(0), y(0), page_pin(), page_row(nullptr), page_cell(nullptr),
          style(), pending_wrap(false) {}
};

/* ─── the screen ─────────────────────────────────────────────────────────── */

struct Screen {
    PageList pages;
    Cursor   cursor;

    Screen() : pages(), cursor() {}
};

/* Point the cursor's cached row and cell at whatever its pin now names.
 *
 * Every operation that moves the cursor or disturbs the pages ends here. The
 * cached pointers are addresses inside a page, so a page replaced for a
 * bigger budget, a list rebuilt by a resize, or a row that scrolled away all
 * leave them pointing at memory that means something else — or nothing. */
inline void screen_cursor_reload(Screen *s) {
    Pin p = s->cursor.page_pin.pin;
    if (!p.valid()) {
        s->cursor.page_row = nullptr;
        s->cursor.page_cell = nullptr;
        return;
    }

    /* The pin carries its own column, and the cursor's x is the truth about
     * where it is, so the pin is brought into line rather than the reverse. */
    p.x = s->cursor.x;
    s->cursor.page_pin.pin.x = s->cursor.x;

    s->cursor.page_row = p.row();
    s->cursor.page_cell = p.cell();
}

/* Move the cursor to a row of the active area, by pin.
 *
 * y is a screen coordinate, so this is where the two ideas of position are
 * reconciled: the row is found by walking from the top of the active area,
 * and the pin that comes back is what will survive the next reflow. */
inline bool screen_cursor_absolute(Screen *s, CellCountInt x, CellCountInt y) {
    if (x >= s->pages.cols || y >= s->pages.rows) return false;

    Pin p = page_list_active_pin(&s->pages, x, y);
    if (!p.valid()) return false;

    s->cursor.x = x;
    s->cursor.y = y;
    s->cursor.page_pin.pin = p;
    s->cursor.pending_wrap = false;
    screen_cursor_reload(s);
    return true;
}

/* Build a screen and put the cursor at the top left. */
inline bool screen_init(Screen *s, CellCountInt cols, CellCountInt rows,
                        size_t max_scrollback_bytes) {
    *s = Screen();
    if (!page_list_init(&s->pages, cols, rows, max_scrollback_bytes)) {
        return false;
    }

    /* The cursor is tracked from the moment it exists. A cursor that was not
     * would be wrong after the first resize, which is not a state worth
     * having even briefly. */
    page_list_track(&s->pages, &s->cursor.page_pin,
                    page_list_active_start(&s->pages));

    return screen_cursor_absolute(s, 0, 0);
}

inline void screen_deinit(Screen *s) {
    page_list_untrack(&s->pages, &s->cursor.page_pin);
    page_list_deinit(&s->pages);
    s->cursor.page_row = nullptr;
    s->cursor.page_cell = nullptr;
}

/* ─── moving ─────────────────────────────────────────────────────────────── */

/* All of these clamp at the edges of the active area and none of them scroll.
 * Scrolling is a decision about the screen as a whole — whether the region
 * moves, whether a line joins the scrollback — and the caller that knows
 * about margins makes it. */

inline void screen_cursor_left(Screen *s, CellCountInt n) {
    const CellCountInt x = s->cursor.x > n ? (CellCountInt)(s->cursor.x - n) : 0;
    screen_cursor_absolute(s, x, s->cursor.y);
}

inline void screen_cursor_right(Screen *s, CellCountInt n) {
    const size_t want = (size_t)s->cursor.x + n;
    const size_t last = (size_t)(s->pages.cols - 1);
    screen_cursor_absolute(s, (CellCountInt)(want > last ? last : want),
                           s->cursor.y);
}

inline void screen_cursor_up(Screen *s, CellCountInt n) {
    const CellCountInt y = s->cursor.y > n ? (CellCountInt)(s->cursor.y - n) : 0;
    screen_cursor_absolute(s, s->cursor.x, y);
}

inline void screen_cursor_down(Screen *s, CellCountInt n) {
    const size_t want = (size_t)s->cursor.y + n;
    const size_t last = (size_t)(s->pages.rows - 1);
    screen_cursor_absolute(s, s->cursor.x,
                           (CellCountInt)(want > last ? last : want));
}

/* Move down a row, scrolling the screen if there is nowhere to move to.
 *
 * This is what a newline at the bottom does, and it is the only cursor
 * operation that changes the text rather than just where it is pointing. The
 * row that leaves the top of the screen is not moved anywhere: the active
 * area is the last rows of the list, so adding a row at the end is what makes
 * the old one scrollback. */
inline bool screen_cursor_down_scroll(Screen *s) {
    if (s->cursor.y + 1 < s->pages.rows) {
        screen_cursor_down(s, 1);
        return true;
    }

    if (!page_list_grow(&s->pages).valid()) return false;

    /* The cursor stays on the last row of the active area, which is now the
     * row that was just added. Its pin was maintained through the growth, but
     * the cached pointers were not. */
    return screen_cursor_absolute(s, s->cursor.x,
                                  (CellCountInt)(s->pages.rows - 1));
}

/* ─── resizing ───────────────────────────────────────────────────────────── */

/* Drop the blank rows below the cursor before a reflow.
 *
 * A screen is almost always mostly empty below the cursor, and those blank
 * rows are not text — they are the rest of the screen. Reflowing them anyway
 * means a line above the cursor that grows from one row to four pushes the
 * cursor off the top of the screen, because the blanks below it still take up
 * the room they used to.
 *
 * So they go first. This is what terminals have always done on a resize, and
 * it belongs here rather than in the list: only a screen knows where its
 * cursor is, and without a cursor there is no way to tell a blank row that is
 * the end of the screen from a blank line somebody printed.
 *
 * Only genuinely empty rows at the very end go, never one that is part of a
 * wrapped line, and never one the cursor or anything else is pinned to. */
inline void screen_trim_blank_rows_below_cursor(Screen *s) {
    const size_t cursor_at =
        page_list_row_index(&s->pages, s->cursor.page_pin.pin);
    if (cursor_at == (size_t)-1) return;

    while (s->pages.row_count > cursor_at + 1) {
        PageNode *last = s->pages.last;
        if (!last || last->rows_used == 0) break;

        const CellCountInt y = (CellCountInt)(last->rows_used - 1);
        Row *row = last->page.get_row(y);

        if (page_row_used_width(&last->page, y) != 0) break;
        if (row->wrap() || row->wrap_continuation()) break;

        /* Something is pinned here, so it is not spare room. */
        bool pinned = false;
        for (TrackedPin *t = s->pages.tracked; t; t = t->next) {
            if (t->pin.node == last && t->pin.y == y) {
                pinned = true;
                break;
            }
        }
        if (pinned) break;

        page_clear_row(&last->page, y);
        last->rows_used--;
        s->pages.row_count--;
    }
}

/* Resize the screen, keeping the cursor on the character it was on.
 *
 * The cursor is a tracked pin, so the reflow moves it; what it does not do is
 * work out which screen coordinates that lands on, because the list has no
 * idea what the cursor is. That is recovered here from the pin's own
 * position. */
inline bool screen_resize(Screen *s, CellCountInt cols, CellCountInt rows) {
    if (cols == 0 || rows == 0) return false;

    if (cols != s->pages.cols) screen_trim_blank_rows_below_cursor(s);

    if (!page_list_resize(&s->pages, cols, rows)) return false;

    Pin p = s->cursor.page_pin.pin;
    if (!p.valid()) {
        page_list_track(&s->pages, &s->cursor.page_pin,
                        page_list_active_start(&s->pages));
        return screen_cursor_absolute(s, 0, 0);
    }

    /* Where the pin ended up, expressed as a screen coordinate again. A pin
     * that reflowed into the scrollback — which is where a cursor near the
     * top of a screen that just got much narrower goes — is clamped to the
     * top of the active area, since a cursor outside the screen is not a
     * thing a terminal can have. */
    const size_t at = page_list_row_index(&s->pages, p);
    const size_t top = page_list_max_scroll(&s->pages);

    CellCountInt y = 0;
    if (at != (size_t)-1 && at > top) {
        const size_t rel = at - top;
        y = (CellCountInt)(rel < (size_t)s->pages.rows
                               ? rel
                               : (size_t)(s->pages.rows - 1));
    }

    CellCountInt x = p.x;
    if (x >= s->pages.cols) x = (CellCountInt)(s->pages.cols - 1);

    return screen_cursor_absolute(s, x, y);
}

/* ─── reading ────────────────────────────────────────────────────────────── */

/* The cell at a screen coordinate, or null if it is off the screen. */
inline Cell *screen_cell(Screen *s, CellCountInt x, CellCountInt y) {
    if (x >= s->pages.cols || y >= s->pages.rows) return nullptr;
    Pin p = page_list_active_pin(&s->pages, x, y);
    return p.valid() ? p.cell() : nullptr;
}

/* The row at a screen coordinate, or null if it is off the screen. */
inline Row *screen_row(Screen *s, CellCountInt y) {
    if (y >= s->pages.rows) return nullptr;
    Pin p = page_list_active_pin(&s->pages, 0, y);
    return p.valid() ? p.row() : nullptr;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_SCREEN_HPP */
