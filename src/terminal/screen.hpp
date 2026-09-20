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
 * PARTIAL PORT. The screen, the cursor, its movement and writing text are
 * here. Erasing, selections, the alternate screen and the saved cursor are
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

    /* DECAWM: whether a character at the right margin wraps to the next line
     * or overwrites the last column.
     *
     * Upstream this is one of Terminal's modes, not the screen's, and it
     * belongs there — it is set by an escape sequence and applies to a
     * terminal rather than to a buffer. It lives here until Terminal is
     * ported, because writing text has to do something at the margin and
     * silently picking one behaviour would be worse than naming it. */
    bool auto_wrap;

    Screen() : pages(), cursor(), auto_wrap(true) {}
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


/* ─── writing ────────────────────────────────────────────────────────────── */

/* Give the cursor's cell the cursor's style, making room if there is none.
 *
 * The cursor carries a style value rather than an interned ID, because an ID
 * belongs to one page's style set and the cursor crosses pages. Interning
 * happens here, at the moment a character is actually written.
 *
 * A page can be out of style slots, and this is where the page budgets earn
 * their keep: the page is replaced by a roomier one holding the same rows.
 * That moves everything to a new address, so the cursor has to be reloaded
 * before the write is retried — a cached cell pointer from before the growth
 * points into a page that has been freed. */
inline bool screen_cursor_apply_style(Screen *s) {
    PageNode *node = s->cursor.page_pin.pin.node;
    if (!node) return false;

    const CellCountInt x = s->cursor.x;
    const CellCountInt y = s->cursor.page_pin.pin.y;

    if (node->page.set_cell_style(x, y, s->cursor.style)) return true;

    PageNode *fresh = page_list_grow_budget(&s->pages, node, PageBudget::styles);
    if (!fresh) return false;

    screen_cursor_reload(s);
    node = s->cursor.page_pin.pin.node;
    return node->page.set_cell_style(s->cursor.x, s->cursor.page_pin.pin.y,
                                     s->cursor.style);
}

/* Erase the cell about to be written, and the other half of any wide
 * character it belongs to.
 *
 * A wide character occupies two cells that only make sense together. Writing
 * over one of them without the other leaves a spacer with nothing to follow,
 * or a lead with nothing after it, and every later reader has to cope with a
 * state the terminal should never have produced. */
inline void screen_erase_for_write(Screen *s, CellCountInt x) {
    Pin p = s->cursor.page_pin.pin;
    if (!p.valid()) return;

    Page *page = &p.node->page;
    const CellCountInt y = p.y;

    Cell *c = page->get_cell(x, y);
    switch (c->wide()) {
        case Wide::wide:
            if ((size_t)x + 1 < (size_t)page->capacity.cols) {
                page_erase_cell(page, (CellCountInt)(x + 1), y);
            }
            break;
        case Wide::spacer_tail:
            if (x > 0) page_erase_cell(page, (CellCountInt)(x - 1), y);
            break;
        default:
            break;
    }

    page_erase_cell(page, x, y);
}

/* Move to the start of the next row, joining it to this one.
 *
 * The wrap flag is what makes the two rows one line, and it is the only
 * record that they were ever joined — without it a resize would treat the
 * break as one the program asked for. */
inline bool screen_wrap(Screen *s) {
    if (s->cursor.page_row) s->cursor.page_row->set_wrap(true);

    if (!screen_cursor_down_scroll(s)) return false;
    if (!screen_cursor_absolute(s, 0, s->cursor.y)) return false;

    if (s->cursor.page_row) s->cursor.page_row->set_wrap_continuation(true);
    s->cursor.pending_wrap = false;
    return true;
}

/* Deal with a cursor that is sitting past the right margin.
 *
 * A character written at the last column does not move the cursor off the
 * end — there is nowhere to move to. It leaves a flag saying the *next*
 * character wraps first. Keeping it as a flag rather than moving the cursor
 * immediately is what makes a line ending exactly at the margin come out
 * right: the wrap only happens if something else is actually written. */
inline bool screen_resolve_pending_wrap(Screen *s) {
    if (!s->cursor.pending_wrap) return true;

    if (!s->auto_wrap) {
        /* With wrapping off the cursor stays at the margin and each new
         * character overwrites the last one. */
        s->cursor.pending_wrap = false;
        return true;
    }

    return screen_wrap(s);
}

/* Write one codepoint at the cursor and move past it.
 *
 * width is 1 or 2 columns, which the caller works out — how wide a codepoint
 * is depends on Unicode tables and on the terminal's own settings, and this
 * has no business knowing about either. */
inline bool screen_write_codepoint(Screen *s, uint32_t cp, int width) {
    if (width != 1 && width != 2) return false;

    const CellCountInt cols = s->pages.cols;

    /* A screen too narrow to hold a wide character at all. Wrapping would not
     * help — the next row is just as narrow — so there is nowhere for it to
     * go, and the alternative to refusing is writing its spacer past the end
     * of the row. */
    if (width == 2 && cols < 2) return false;

    if (!screen_resolve_pending_wrap(s)) return false;

    if (width == 2) {
        /* A wide character will not be split across the right margin. The
         * column it cannot use is marked so that a reflow later knows the gap
         * was the margin's doing and not something the program printed. */
        if ((size_t)s->cursor.x + 1 >= (size_t)cols) {
            if (!s->auto_wrap) return true;

            screen_erase_for_write(s, s->cursor.x);
            Cell *pad = s->cursor.page_cell;
            *pad = Cell();
            pad->set_wide(Wide::spacer_head);

            if (!screen_wrap(s)) return false;
        }
    }

    screen_erase_for_write(s, s->cursor.x);
    if (!screen_cursor_apply_style(s)) return false;

    Cell *c = s->cursor.page_cell;
    c->set_content_tag(ContentTag::codepoint);
    c->set_codepoint(cp);
    c->set_wide(width == 2 ? Wide::wide : Wide::narrow);
    if (s->cursor.page_row) s->cursor.page_row->set_dirty(true);

    if (width == 2) {
        Pin p = s->cursor.page_pin.pin;
        Cell *tail = p.node->page.get_cell((CellCountInt)(s->cursor.x + 1), p.y);
        *tail = Cell();
        tail->set_wide(Wide::spacer_tail);
    }

    const size_t next = (size_t)s->cursor.x + (size_t)width;
    if (next >= (size_t)cols) {
        /* No column left to move to, so the cursor stays where it is and the
         * next character deals with it. */
        s->cursor.pending_wrap = true;
        return true;
    }

    return screen_cursor_absolute(s, (CellCountInt)next, s->cursor.y);
}

/* Add a codepoint to the character just written.
 *
 * Combining marks arrive after the character they modify, so this attaches to
 * the cell behind the cursor rather than the one under it. A cursor that has
 * not written anything on this row has nothing to attach to. */
inline bool screen_append_grapheme(Screen *s, uint32_t cp) {
    Pin p = s->cursor.page_pin.pin;
    if (!p.valid()) return false;

    /* The cell the last character went into: the one under the cursor when a
     * wrap is pending, otherwise the one before it. */
    CellCountInt x = s->cursor.x;
    if (!s->cursor.pending_wrap) {
        if (x == 0) return false;
        x = (CellCountInt)(x - 1);
    }

    Page *page = &p.node->page;

    /* A wide character's spacer is not where its codepoints live. */
    if (page->get_cell(x, p.y)->wide() == Wide::spacer_tail) {
        if (x == 0) return false;
        x = (CellCountInt)(x - 1);
    }

    if (page_append_grapheme(page, x, p.y, cp)) return true;

    /* Out of grapheme storage: the same answer as running out of styles. */
    PageNode *fresh = page_list_grow_budget(&s->pages, p.node,
                                            PageBudget::grapheme_bytes);
    if (!fresh) return false;

    screen_cursor_reload(s);
    p = s->cursor.page_pin.pin;
    return page_append_grapheme(&p.node->page, x, p.y, cp);
}

/* Write a run of narrow ASCII, which is what almost all output is. */
inline bool screen_write_ascii(Screen *s, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!screen_write_codepoint(s, (uint32_t)(unsigned char)text[i], 1)) {
            return false;
        }
    }
    return true;
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
