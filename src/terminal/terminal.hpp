/* Ported from Ghostty src/terminal/Terminal.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * The terminal: the thing a program is actually talking to.
 *
 * A Screen is a buffer with a cursor over it, and it has no opinions. It does
 * not know that a line feed is different from moving down, that a tab goes to
 * the next tab stop, or that DECAWM exists. Those are properties of a
 * terminal — of the machine being emulated — and this is where they live.
 *
 * Two things arrive here that nothing below could hold:
 *
 * The modes. A terminal's behaviour is settable by escape sequence, and a
 * mode is not state about the text but state about what the next character
 * will do to it. auto_wrap has been sitting on Screen since writing text was
 * ported, with a note saying it belonged here; this is where it moves to, and
 * the Screen's copy becomes something Terminal keeps in step rather than
 * something anyone else sets.
 *
 * The second screen. A terminal owns two: the primary one, with its
 * scrollback, and the alternate one that full-screen programs draw on. They
 * are the same kind of thing, which is why a Screen has no idea there is
 * another — swapping between them is the terminal's business.
 *
 * NOT THE LIVE TERMINAL. Wisp's working emulator is the C code under
 * src/terminal. This is the ported one, built on the ported screen, and
 * nothing runs on it yet.
 *
 * PARTIAL PORT. The terminal, its modes, both screens, printing, the C0
 * control characters and tab stops are here. Scroll regions, the escape
 * sequence dispatch and the rest of the modes are not, so the ledger records
 * Terminal.zig as `wip`.
 */

#pragma once
#ifndef WISP_TERMINAL_TERMINAL_HPP
#define WISP_TERMINAL_TERMINAL_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "screen.hpp"

namespace wisp {
namespace terminal {

/* ─── modes ──────────────────────────────────────────────────────────────── */

/* The settable behaviours, by their DEC numbers where they have them.
 *
 * Only the ones something already does are here. A mode nobody reads would be
 * a field that looks like it works. */
struct Modes {
    /* DECAWM (?7): a character at the right margin wraps to the next line
     * rather than overwriting the last column. On by default, which is what
     * every terminal has done since the VT100. */
    bool wraparound;

    /* IRM (4): printing pushes what is already on the line to the right
     * rather than writing over it. */
    bool insert;

    /* DECCKM (?1): the arrow keys send application sequences. Nothing here
     * generates key sequences yet, so this is carried rather than used —
     * it is set by the same escape sequence as the others and losing it
     * would mean a program that sets it and asks for it back getting a
     * different answer. */
    bool cursor_keys;

    /* ?1049: the alternate screen is showing. Kept as a mode because that is
     * how a program asks about it, but the truth is which screen is active. */
    bool alt_screen;

    Modes()
        : wraparound(true), insert(false), cursor_keys(false),
          alt_screen(false) {}
};

/* ─── the terminal ───────────────────────────────────────────────────────── */

/* How far apart tab stops are when there is nothing better to say.
 *
 * Every terminal since the teletype has used eight, and programs that draw
 * columns assume it. */
static const CellCountInt TAB_INTERVAL = 8;

struct Terminal {
    /* Both screens exist for the terminal's whole life. Making the alternate
     * one on demand would mean an allocation failing in the middle of an
     * escape sequence, which is not a failure anything could do something
     * useful with. */
    Screen primary;
    Screen alternate;

    /* Which of the two is being written to. */
    Screen *active;

    Modes modes;

    CellCountInt cols;
    CellCountInt rows;

    /* One byte per column: is there a tab stop here. A bitset would be eight
     * times smaller and a great deal less obvious, for a few hundred bytes. */
    uint8_t *tabs;

    Terminal()
        : primary(), alternate(), active(nullptr), modes(), cols(0), rows(0),
          tabs(nullptr) {}
};

/* Put tab stops back to every eighth column. */
inline void terminal_reset_tabs(Terminal *t) {
    if (!t->tabs) return;
    memset(t->tabs, 0, t->cols);
    for (CellCountInt x = TAB_INTERVAL; x < t->cols; x += TAB_INTERVAL) {
        t->tabs[x] = 1;
    }
}

inline void terminal_deinit(Terminal *t) {
    screen_deinit(&t->primary);
    screen_deinit(&t->alternate);
    free(t->tabs);
    t->tabs = nullptr;
    t->active = nullptr;
}

inline bool terminal_init(Terminal *t, CellCountInt cols, CellCountInt rows,
                          size_t max_scrollback_bytes) {
    *t = Terminal();
    t->cols = cols;
    t->rows = rows;

    if (!screen_init(&t->primary, cols, rows, max_scrollback_bytes)) {
        return false;
    }

    /* The alternate screen keeps nothing. Scrollback there would be a record
     * of half-drawn frames rather than of output, since the programs that use
     * it redraw the whole display whenever they feel like it. */
    if (!screen_init(&t->alternate, cols, rows, 0)) {
        screen_deinit(&t->primary);
        return false;
    }
    t->alternate.pages.no_scrollback = true;

    t->tabs = (uint8_t *)malloc(cols);
    if (!t->tabs) {
        terminal_deinit(t);
        return false;
    }
    terminal_reset_tabs(t);

    t->active = &t->primary;
    t->active->auto_wrap = t->modes.wraparound;
    return true;
}

/* ─── modes ──────────────────────────────────────────────────────────────── */

/* Push the modes that a screen needs to know about down to it.
 *
 * Only wraparound so far. The screen has a copy rather than a pointer because
 * writing a character reads it once per character, and because a Screen that
 * reached back up into a Terminal would be the wrong way round. */
inline void terminal_apply_modes(Terminal *t) {
    t->primary.auto_wrap = t->modes.wraparound;
    t->alternate.auto_wrap = t->modes.wraparound;
}

/* ─── the alternate screen ───────────────────────────────────────────────── */

/* Switch to the alternate screen, as ?1049 does.
 *
 * The cursor is saved on the way in and the alternate screen is cleared, so a
 * program gets a blank display and the shell gets its prompt back afterwards
 * exactly where it was. Doing the save here rather than leaving it to the
 * program is the difference between ?1049 and the older ?47, and it is why
 * ?1049 is the one everything uses. */
inline void terminal_alt_screen_enter(Terminal *t) {
    if (t->modes.alt_screen) return;

    screen_save_cursor(&t->primary);

    t->active = &t->alternate;
    t->modes.alt_screen = true;

    screen_select_clear(&t->alternate);
    screen_cursor_absolute(&t->alternate, 0, 0);
    t->alternate.cursor.style = style::Style();
    screen_erase_display(&t->alternate, 2, false);
    screen_cursor_absolute(&t->alternate, 0, 0);
}

/* Switch back, restoring the cursor the way in put away. */
inline void terminal_alt_screen_leave(Terminal *t) {
    if (!t->modes.alt_screen) return;

    t->active = &t->primary;
    t->modes.alt_screen = false;

    screen_restore_cursor(&t->primary);
}

/* ─── printing ───────────────────────────────────────────────────────────── */

/* Print a character.
 *
 * width is how many columns the codepoint occupies, which depends on Unicode
 * tables the terminal is configured with rather than on anything here. */
inline bool terminal_print(Terminal *t, uint32_t cp, int width) {
    Screen *s = t->active;

    if (t->modes.insert) {
        /* IRM: what is on the line moves right to make room, and whatever
         * falls off the end is gone. */
        const CellCountInt x = s->cursor.x;
        if ((size_t)x + width < (size_t)t->cols) {
            Pin p = page_list_active_pin(&s->pages, 0, s->cursor.y);
            if (p.valid()) {
                Page *page = &p.node->page;
                for (size_t i = t->cols; i-- > (size_t)x + width;) {
                    page_erase_cell(page, (CellCountInt)i, p.y);
                    if (!page_clone_cell(page, (CellCountInt)i, p.y, page,
                                         (CellCountInt)(i - width), p.y)) {
                        break;
                    }
                }
            }
            for (int i = 0; i < width; i++) {
                screen_clear_cells(s, s->cursor.y, (CellCountInt)(x + i), 1,
                                   false);
            }
            screen_cursor_reload(s);
        }
    }

    return screen_write_codepoint(s, cp, width);
}

/* Attach a codepoint to the character just printed. */
inline bool terminal_print_combining(Terminal *t, uint32_t cp) {
    return screen_append_grapheme(t->active, cp);
}

/* ─── the C0 controls ────────────────────────────────────────────────────── */

/* CR — back to the first column, same row. */
inline void terminal_carriage_return(Terminal *t) {
    Screen *s = t->active;
    screen_cursor_absolute(s, 0, s->cursor.y);
}

/* LF, and IND — down a row, scrolling the screen if there is nowhere to go.
 *
 * The column does not change, which is what distinguishes a line feed from a
 * new line: a terminal in its default state answers LF with the cursor
 * directly below where it was, and it is the CR that brings it back. */
inline bool terminal_linefeed(Terminal *t) {
    return screen_cursor_down_scroll(t->active);
}

/* RI — up a row. At the top of the screen the display scrolls down and a
 * blank line appears above, which is what makes a program able to insert a
 * line at the top without redrawing everything below it. */
inline bool terminal_reverse_index(Terminal *t) {
    Screen *s = t->active;
    if (s->cursor.y > 0) {
        screen_cursor_up(s, 1);
        return true;
    }

    /* Everything on the screen moves down one, and the top row is blank.
     * Unlike a line feed this loses the bottom row rather than putting it in
     * the scrollback: it never left the top of the screen. */
    for (CellCountInt y = (CellCountInt)(t->rows - 1); y > 0; y--) {
        Pin dst = page_list_active_pin(&s->pages, 0, y);
        Pin src = page_list_active_pin(&s->pages, 0, (CellCountInt)(y - 1));
        if (!dst.valid() || !src.valid()) return false;
        if (!page_clone_row(&dst.node->page, dst.y, &src.node->page, src.y)) {
            return false;
        }
    }

    screen_cursor_absolute(s, s->cursor.x, 0);
    screen_clear_cells(s, 0, 0, t->cols, false);
    screen_cursor_reload(s);
    return true;
}

/* BS — back one column, stopping at the left margin. */
inline void terminal_backspace(Terminal *t) {
    screen_cursor_left(t->active, 1);
}

/* ─── tab stops ──────────────────────────────────────────────────────────── */

/* HTS — a tab stop here. */
inline void terminal_tab_set(Terminal *t) {
    if (t->active->cursor.x < t->cols) t->tabs[t->active->cursor.x] = 1;
}

/* TBC — clear the stop here (0) or every stop (3). */
inline void terminal_tab_clear(Terminal *t, int mode) {
    if (mode == 0) {
        if (t->active->cursor.x < t->cols) t->tabs[t->active->cursor.x] = 0;
    } else if (mode == 3) {
        memset(t->tabs, 0, t->cols);
    }
}

/* HT — forward to the next tab stop, or to the last column if there is none.
 *
 * A tab moves the cursor; it does not write spaces. Anything already in the
 * cells it passes over stays there, which is how a program can tab back over
 * its own output and have it still be visible. */
inline void terminal_horizontal_tab(Terminal *t, CellCountInt n) {
    if (n == 0) n = 1;
    Screen *s = t->active;

    for (CellCountInt i = 0; i < n; i++) {
        CellCountInt x = s->cursor.x;
        CellCountInt to = (CellCountInt)(t->cols - 1);

        for (CellCountInt c = (CellCountInt)(x + 1); c < t->cols; c++) {
            if (t->tabs[c]) {
                to = c;
                break;
            }
        }

        screen_cursor_absolute(s, to, s->cursor.y);
        if (to == (CellCountInt)(t->cols - 1)) break;
    }
}

/* CBT — back to the previous tab stop. */
inline void terminal_reverse_tab(Terminal *t, CellCountInt n) {
    if (n == 0) n = 1;
    Screen *s = t->active;

    for (CellCountInt i = 0; i < n; i++) {
        CellCountInt to = 0;
        for (CellCountInt c = s->cursor.x; c-- > 0;) {
            if (t->tabs[c]) {
                to = c;
                break;
            }
        }

        screen_cursor_absolute(s, to, s->cursor.y);
        if (to == 0) break;
    }
}

/* ─── resizing ───────────────────────────────────────────────────────────── */

/* Resize both screens.
 *
 * The one that is not showing is resized too. A program on the alternate
 * screen expects the shell underneath to have noticed the new size by the
 * time it exits, and a primary screen left at the old width would reflow on
 * the way back at exactly the moment the user is looking at it. */
inline bool terminal_resize(Terminal *t, CellCountInt cols, CellCountInt rows) {
    if (cols == 0 || rows == 0) return false;

    if (!screen_resize(&t->primary, cols, rows)) return false;
    if (!screen_resize(&t->alternate, cols, rows)) return false;

    if (cols != t->cols) {
        uint8_t *tabs = (uint8_t *)malloc(cols);
        if (!tabs) return false;
        free(t->tabs);
        t->tabs = tabs;
        t->cols = cols;
        /* The stops go back to their defaults rather than being carried
         * across. A stop is a column number, and the columns have changed. */
        terminal_reset_tabs(t);
    }

    t->cols = cols;
    t->rows = rows;
    return true;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_TERMINAL_HPP */
