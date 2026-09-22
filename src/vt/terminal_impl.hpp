/* Transliterated from Ghostty src/terminal/Terminal.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Out-of-line definitions for terminal.hpp (included from its end).
 *
 * TRANSLITERATION. Comments are upstream's unless marked "Wisp:".
 */

#pragma once
#ifndef WISP_VT_TERMINAL_IMPL_HPP
#define WISP_VT_TERMINAL_IMPL_HPP

namespace wisp {
namespace vt {

/* Set the charset into the given slot. */
inline void Terminal::configureCharset(terminal::charsets::Slots slot, terminal::charsets::Charset set) {
    screens.active->charset.charsets.set(slot, set);
}

/* Invoke the charset in slot into the active slot. If single is true,
 * then this will only be invoked for a single character. */
inline void Terminal::invokeCharset(terminal::charsets::ActiveSlot active, terminal::charsets::Slots slot,
                                    bool single) {
    typedef terminal::charsets::ActiveSlot ActiveSlot;
    if (single) {
        assert(active == ActiveSlot::GL);
        screens.active->charset.single_shift = slot;
        return;
    }

    switch (active) {
    case ActiveSlot::GL: screens.active->charset.gl = slot; break;
    case ActiveSlot::GR: screens.active->charset.gr = slot; break;
    }
}

/* Carriage return moves the cursor to the first column. */
inline void Terminal::carriageReturn() {
    /* Always reset pending wrap state */
    screens.active->cursor.pending_wrap = false;

    /* In origin mode we always move to the left margin */
    size::CellCountInt x;
    if (modes.get(terminal::modes::Mode::origin)) {
        x = scrolling_region.left;
    } else if (screens.active->cursor.x >= scrolling_region.left) {
        x = scrolling_region.left;
    } else {
        x = 0;
    }
    screens.active->cursorHorizontalAbsolute(x);
}

/* Linefeed moves the cursor to the next line. */
inline bool Terminal::linefeed() {
    if (!index()) return false;
    if (modes.get(terminal::modes::Mode::linefeed)) carriageReturn();
    return true;
}

/* Backspace moves the cursor back a column (but not less than 0). */
inline void Terminal::backspace() { cursorLeft(1); }

/* Move the cursor up amount lines. If amount is greater than the maximum
 * move distance then it is internally adjusted to the maximum. If amount is
 * 0, adjust it to 1. */
inline void Terminal::cursorUp(size_t count_req) {
    /* Always resets pending wrap */
    screens.active->cursor.pending_wrap = false;

    /* The maximum amount the cursor can move up depends on scrolling regions */
    const size_t max = screens.active->cursor.y >= scrolling_region.top
                           ? (size_t)(screens.active->cursor.y - scrolling_region.top)
                           : (size_t)screens.active->cursor.y;
    const size_t req = count_req > 1 ? count_req : 1;
    const size_t count = max < req ? max : req;

    /* We can safely intCast below because of the min/max clamping we did above. */
    screens.active->cursorUp((size::CellCountInt)count);
}

/* Move the cursor down amount lines. If amount is greater than the maximum
 * move distance then it is internally adjusted to the maximum. This sequence
 * will not scroll the screen or scroll region. If amount is 0, adjust it to 1. */
inline void Terminal::cursorDown(size_t count_req) {
    /* Always resets pending wrap */
    screens.active->cursor.pending_wrap = false;

    /* The max the cursor can move to depends where the cursor currently is */
    const size_t max = screens.active->cursor.y <= scrolling_region.bottom
                           ? (size_t)(scrolling_region.bottom - screens.active->cursor.y)
                           : (size_t)(rows - screens.active->cursor.y - 1);
    const size_t req = count_req > 1 ? count_req : 1;
    const size_t count = max < req ? max : req;
    screens.active->cursorDown((size::CellCountInt)count);
}

/* Move the cursor right amount columns. If amount is greater than the
 * maximum move distance then it is internally adjusted to the maximum.
 * This sequence will not scroll the screen or scroll region. If amount is
 * 0, adjust it to 1. */
inline void Terminal::cursorRight(size_t count_req) {
    /* Always resets pending wrap */
    screens.active->cursor.pending_wrap = false;

    /* The max the cursor can move to depends where the cursor currently is */
    const size_t max = screens.active->cursor.x <= scrolling_region.right
                           ? (size_t)(scrolling_region.right - screens.active->cursor.x)
                           : (size_t)(cols - screens.active->cursor.x - 1);
    const size_t req = count_req > 1 ? count_req : 1;
    const size_t count = max < req ? max : req;
    screens.active->cursorRight((size::CellCountInt)count);
}

/* Move the cursor to the left amount cells. If amount is 0, adjust it to 1. */
inline void Terminal::cursorLeft(size_t count_req) {
    typedef terminal::modes::Mode Mode;

    /* Wrapping behavior depends on various terminal modes */
    enum class WrapMode { none, reverse, reverse_extended };
    WrapMode wrap_mode;
    if (!modes.get(Mode::wraparound)) {
        wrap_mode = WrapMode::none;
    } else if (modes.get(Mode::reverse_wrap_extended)) {
        wrap_mode = WrapMode::reverse_extended;
    } else if (modes.get(Mode::reverse_wrap)) {
        wrap_mode = WrapMode::reverse;
    } else {
        wrap_mode = WrapMode::none;
    }

    size_t count = count_req > 1 ? count_req : 1;

    /* If we are in no wrap mode, then we move the cursor left and exit
     * since this is the fastest and most typical path. */
    if (wrap_mode == WrapMode::none) {
        const size_t x = screens.active->cursor.x;
        screens.active->cursorLeft((size::CellCountInt)(count < x ? count : x));
        screens.active->cursor.pending_wrap = false;
        return;
    }

    /* If we have a pending wrap state and we are in either reverse wrap
     * modes then we decrement the amount we move by one to match xterm. */
    if (screens.active->cursor.pending_wrap) {
        count -= 1;
        screens.active->cursor.pending_wrap = false;
    }

    /* The margins we can move to. */
    const size::CellCountInt top = scrolling_region.top;
    const size::CellCountInt bottom = scrolling_region.bottom;
    const size::CellCountInt right_margin = scrolling_region.right;
    const size::CellCountInt left_margin =
        screens.active->cursor.x < scrolling_region.left ? (size::CellCountInt)0 : scrolling_region.left;

    /* Handle some edge cases when our cursor is already on the left margin. */
    if (screens.active->cursor.x == left_margin) {
        switch (wrap_mode) {
        /* In reverse mode, if we're already before the top margin
         * then we just set our cursor to the top-left and we're done. */
        case WrapMode::reverse:
            if (screens.active->cursor.y <= top) {
                screens.active->cursorAbsolute(left_margin, top);
                return;
            }
            break;

        /* Handled in while loop */
        case WrapMode::reverse_extended: break;

        /* Handled above */
        case WrapMode::none: assert(false); break;
        }
    }

    for (;;) {
        /* We can move at most to the left margin. */
        const size_t max = (size_t)(screens.active->cursor.x - left_margin);

        /* We want to move at most the number of columns we have left
         * or our remaining count. Do the move. */
        const size_t amount = max < count ? max : count;
        count -= amount;
        screens.active->cursorLeft((size::CellCountInt)amount);

        /* If we have no more to move, then we're done. */
        if (count == 0) break;

        /* If we are at the top, then we are done. */
        if (screens.active->cursor.y == top) {
            if (wrap_mode != WrapMode::reverse_extended) break;

            screens.active->cursorAbsolute(right_margin, bottom);
            count -= 1;
            continue;
        }

        /* UNDEFINED TERMINAL BEHAVIOR. This situation is not handled in xterm
         * and currently results in a crash in xterm. Given no other known
         * terminal [to me] implements XTREVWRAP2, I decided to just mimic
         * the behavior of xterm up and not including the crash by wrapping
         * up to the (0, 0) and stopping there. My reasoning is that for an
         * appropriately sized value of "count" this is the behavior that xterm
         * would have. This is unit tested. */
        if (screens.active->cursor.y == 0) {
            assert(screens.active->cursor.x == left_margin);
            break;
        }

        /* If our previous line is not wrapped then we are done. */
        if (wrap_mode != WrapMode::reverse_extended) {
            const Row *prev_row = screens.active->cursorRowUp(1);
            if (!prev_row->wrap()) break;
        }

        screens.active->cursorAbsolute(right_margin, (size::CellCountInt)(screens.active->cursor.y - 1));
        count -= 1;
    }
}

/* Save cursor position and further state.
 *
 * The primary and alternate screen have distinct save state. One saved state
 * is kept per screen (main / alternative). If for the current screen state
 * was already saved it is overwritten. */
inline void Terminal::saveCursor() {
    Screen::SavedCursor saved;
    saved.x = screens.active->cursor.x;
    saved.y = screens.active->cursor.y;
    saved.style = screens.active->cursor.style;
    saved.protected_ = screens.active->cursor.protected_;
    saved.pending_wrap = screens.active->cursor.pending_wrap;
    saved.origin = modes.get(terminal::modes::Mode::origin);
    saved.charset = screens.active->charset;
    screens.active->saved_cursor = saved;
}

/* Restore cursor position and other state.
 *
 * The primary and alternate screen have distinct save state.
 * If no save was done before values are reset to their initial values. */
inline void Terminal::restoreCursor() {
    Screen::SavedCursor saved;
    if (screens.active->saved_cursor.has) {
        saved = screens.active->saved_cursor.value;
    } else {
        saved.x = 0;
        saved.y = 0;
        saved.style = style::Style();
        saved.protected_ = false;
        saved.pending_wrap = false;
        saved.origin = false;
        saved.charset = Screen::CharsetState();
    }

    /* Set the style first because it can fail */
    screens.active->cursor.style = saved.style;
    if (screens.active->manualStyleUpdate() != PageList::IncreaseCapacityError::none) {
        /* Regardless of the error here, we revert back to an unstyled
         * cursor. It is more important that the restore succeeds in
         * other attributes because terminals have no way to communicate
         * failure back.
         * log.warn("restoreCursor error updating style err={}") */
        Screen *screen = screens.active;
        screen->cursor.style = style::Style();
        const PageList::IncreaseCapacityError e = screens.active->manualStyleUpdate();
        assert(e == PageList::IncreaseCapacityError::none);
        (void)e;
    }

    screens.active->charset = saved.charset;
    modes.set(terminal::modes::Mode::origin, saved.origin);
    screens.active->cursor.pending_wrap = saved.pending_wrap;
    screens.active->cursor.protected_ = saved.protected_;
    screens.active->cursorAbsolute((size::CellCountInt)(saved.x < cols - 1 ? saved.x : cols - 1),
                                   (size::CellCountInt)(saved.y < rows - 1 ? saved.y : rows - 1));

    /* Ensure our screen is consistent */
    screens.active->assertIntegrity();
}

/* Set the character protection mode for the terminal. */
inline void Terminal::setProtectedMode(terminal::ansi::ProtectedMode mode) {
    typedef terminal::ansi::ProtectedMode ProtectedMode;
    switch (mode) {
    case ProtectedMode::off:
        screens.active->cursor.protected_ = false;

        /* screen.protected_mode is NEVER reset to ".off" because
         * logic such as eraseChars depends on knowing what the
         * _most recent_ mode was. */
        break;

    case ProtectedMode::iso:
        screens.active->cursor.protected_ = true;
        screens.active->protected_mode = ProtectedMode::iso;
        break;

    case ProtectedMode::dec:
        screens.active->cursor.protected_ = true;
        screens.active->protected_mode = ProtectedMode::dec;
        break;
    }
}

/* Print UTF-8 encoded string to the terminal. */
inline bool Terminal::printString(const char *str, size_t len) {
    /* Wisp: std.unicode.Utf8View iterator. */
    const uint8_t *s = (const uint8_t *)str;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        const uint8_t c0 = s[i];
        size_t clen;
        if (c0 < 0x80) {
            cp = c0;
            clen = 1;
        } else if ((c0 >> 5) == 6) {
            cp = c0 & 0x1F;
            clen = 2;
        } else if ((c0 >> 4) == 14) {
            cp = c0 & 0x0F;
            clen = 3;
        } else {
            cp = c0 & 0x07;
            clen = 4;
        }
        for (size_t k = 1; k < clen; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        i += clen;

        switch (cp) {
        case '\n':
            carriageReturn();
            if (!linefeed()) return false;
            break;

        default:
            if (!print(cp)) return false;
            break;
        }
    }
    return true;
}

inline bool Terminal::print(uint32_t c) {
    typedef terminal::modes::Mode Mode;
    /* log.debug("print={x} y={} x={}") */

    /* If we're not on the main display, do nothing for now */
    if (status_display != terminal::ansi::StatusDisplay::main) {
        return true;
    }

    /* After doing any printing, wrapping, scrolling, etc. we want to ensure
     * that our screen remains in a consistent state. */
    struct Guard {
        Screen *s;
        ~Guard() { s->assertIntegrity(); }
    } guard = {screens.active};
    (void)guard;

    /* Our right margin depends where our cursor is now. */
    const size::CellCountInt right_limit = screens.active->cursor.x > scrolling_region.right
                                               ? cols
                                               : (size::CellCountInt)(scrolling_region.right + 1);

    /* Perform grapheme clustering if grapheme support is enabled (mode 2027).
     * This is MUCH slower than the normal path so the conditional below is
     * purposely ordered in least-likely to most-likely so we can drop out
     * as quickly as possible. */
    if (c > 255 && modes.get(Mode::grapheme_cluster) && screens.active->cursor.x > 0) {
        bool grapheme_done = false;
        do {
            /* We need the previous cell to determine if we're at a grapheme
             * break or not. If we are NOT, then we are still combining the
             * same grapheme, and will be appending to prev.cell. Otherwise, we are
             * in a new cell. */
            struct Prev {
                Cell *cell;
                size::CellCountInt left;
            } prev;
            {
                size::CellCountInt left;
                /* If we have wraparound, then we use the prev col unless
                 * there's a pending wrap, in which case we use the current. */
                if (modes.get(Mode::wraparound)) {
                    left = screens.active->cursor.pending_wrap ? 0 : 1;
                } else if (screens.active->cursor.x != right_limit - 1) {
                    /* If we do not have wraparound, the logic is trickier. If
                     * we're not on the last column, then we just use the previous
                     * column. Otherwise, we need to check if there is text to
                     * figure out if we're attaching to the prev or current. */
                    left = 1;
                } else {
                    left = screens.active->cursor.page_cell->codepoint() == 0 ? 1 : 0;
                }

                /* If the previous cell is a wide spacer tail, then we actually
                 * want to use the cell before that because that has the actual
                 * content. */
                Cell *immediate = screens.active->cursorCellLeft(left);
                if (immediate->wide() == Cell::Wide::spacer_tail) {
                    prev.cell = screens.active->cursorCellLeft((size::CellCountInt)(left + 1));
                    prev.left = (size::CellCountInt)(left + 1);
                } else {
                    prev.cell = immediate;
                    prev.left = left;
                }
            }

            /* If our cell has no content, then this is a new cell and
             * necessarily a grapheme break. */
            if (prev.cell->codepoint() == 0) break;

            uint32_t previous_codepoint = prev.cell->contentCodepoint();
            bool grapheme_break;
            {
                unicode::BreakState state;
                if (prev.cell->hasGrapheme()) {
                    size_t cps_len = 0;
                    const uint32_t *cps =
                        screens.active->cursor.page_pin->node->page()->lookupGrapheme(prev.cell, &cps_len);
                    for (size_t gi = 0; gi < cps_len; gi++) {
                        const uint32_t cp2 = cps[gi];
                        /* With mode 2027 disabled, zero-width codepoints are
                         * attached without applying grapheme boundary rules. If
                         * the mode is enabled later, an existing cell can
                         * therefore contain one or more breaks. Feed those breaks
                         * into the state machine so it can reset its context and
                         * determine the boundary for the new codepoint. */
                        (void)unicode::graphemeBreak(previous_codepoint, cp2, &state);
                        previous_codepoint = cp2;
                    }
                }

                grapheme_break = unicode::graphemeBreak(previous_codepoint, c, &state);
            }

            /* If we can NOT break, this means that "c" is part of a grapheme
             * with the previous char. */
            if (!grapheme_break) {
                switch (unicode::graphemeWidthEffect(previous_codepoint, c)) {
                case unicode::GraphemeWidthEffect::ignore: return true;
                case unicode::GraphemeWidthEffect::wide: {
                    if (prev.cell->wide() == Cell::Wide::wide) break;

                    /* Move our cursor back to the previous. We'll move
                     * the cursor within this block to the proper location. */
                    screens.active->cursorLeft(prev.left);

                    /* If we don't have space for the wide char, we need to
                     * insert spacers and wrap. We need special handling if the
                     * previous cell has grapheme data. */
                    if (screens.active->cursor.x == right_limit - 1) {
                        if (!modes.get(Mode::wraparound)) return true;

                        /* This path can write a spacer_head before printWrap
                         * which can trigger integrity violations so mark
                         * the wrap first to keep the intermediary state valid
                         * if we're wrapping. */
                        const bool row_wrap = right_limit == cols;
                        if (row_wrap) screens.active->cursor.page_row->setWrap(true);

                        const uint32_t prev_cp = prev.cell->contentCodepoint();
                        if (prev.cell->hasGrapheme()) {
                            /* This is like printCell but without clearing the
                             * grapheme data from the cell, so we can move it
                             * later. */
                            prev.cell->setWide(row_wrap ? Cell::Wide::spacer_head : Cell::Wide::narrow);
                            prev.cell->setContentCodepoint(0);

                            if (!printWrap()) return false;
                            printCell(prev_cp, Cell::Wide::wide);

                            const Pin new_pin = *screens.active->cursor.page_pin;
                            const Page::RowAndCell new_rac = new_pin.rowAndCell();

                            do { /* transfer_graphemes */
                                const Maybe<Pin> old_pin_ = screens.active->cursor.page_pin->up(1);
                                if (!old_pin_.has) break;
                                Pin old_pin = old_pin_.value;
                                old_pin.x = (size::CellCountInt)(right_limit - 1);
                                const Page::RowAndCell old_rac = old_pin.rowAndCell();

                                if (new_pin.node == old_pin.node) {
                                    new_pin.node->page()->moveGrapheme(old_rac.cell, new_rac.cell);
                                    old_rac.cell->setContentTag(Cell::ContentTag::codepoint);
                                    new_rac.cell->setContentTag(Cell::ContentTag::codepoint_grapheme);
                                    new_rac.row->setGrapheme(true);
                                } else {
                                    size_t cps_len = 0;
                                    const uint32_t *cps =
                                        old_pin.node->page()->lookupGrapheme(old_rac.cell, &cps_len);
                                    for (size_t gi = 0; gi < cps_len; gi++) {
                                        /* appendGrapheme can grow the cursor
                                         * page, so read the destination from
                                         * the cursor each time rather than
                                         * holding a pointer across the call. */
                                        if (screens.active->appendGrapheme(screens.active->cursor.page_cell,
                                                                           cps[gi]) !=
                                            PageList::IncreaseCapacityError::none)
                                            return false;
                                    }
                                    old_pin.node->page()->clearGrapheme(old_rac.cell);
                                }

                                old_pin.node->page()->updateRowGraphemeFlag(old_rac.row);
                            } while (false);

                            /* Point prev.cell to our new previous cell that
                             * we'll be appending graphemes to */
                            prev.cell = screens.active->cursor.page_cell;
                        } else {
                            printCell(0, row_wrap ? Cell::Wide::spacer_head : Cell::Wide::narrow);
                            if (!printWrap()) return false;
                            printCell(prev_cp, Cell::Wide::wide);

                            /* Point prev.cell to our new previous cell that
                             * we'll be appending graphemes to */
                            prev.cell = screens.active->cursor.page_cell;
                        }
                    } else {
                        prev.cell->setWide(Cell::Wide::wide);
                    }

                    /* Write our spacer, since prev.cell is now wide */
                    screens.active->cursorRight(1);

                    /* Writing the spacer can grow the page to make room for
                     * the cursor hyperlink. Growing replaces the page, which
                     * invalidates `prev.cell`. Record the page identity first
                     * so the common case where nothing grows stays free.
                     *
                     * A pointer comparison alone isn't enough: pages are
                     * pooled, so a replacement can reuse the same address.
                     * The serial makes the pair a unique identity. */
                    PageList::Node *spacer_node = screens.active->cursor.page_pin->node;
                    const uint64_t spacer_serial = spacer_node->serial;

                    printCell(0, Cell::Wide::spacer_tail);

                    if (screens.active->cursor.page_pin->node != spacer_node ||
                        screens.active->cursor.page_pin->node->serial != spacer_serial) {
                        /* The cursor is on the spacer tail we just wrote, so
                         * the wide cell we append to is the one to its left. */
                        prev.cell = screens.active->cursorCellLeft(1);
                    }

                    /* Move the cursor again so we're beyond our spacer */
                    if (screens.active->cursor.x == right_limit - 1) {
                        screens.active->cursor.pending_wrap = true;
                    } else {
                        screens.active->cursorRight(1);
                    }
                    break;
                }

                case unicode::GraphemeWidthEffect::narrow: {
                    /* Prev cell is no longer wide */
                    if (prev.cell->wide() != Cell::Wide::wide) break;
                    prev.cell->setWide(Cell::Wide::narrow);

                    /* Remove the wide spacer tail. The previous cell may be
                     * under the cursor, so locate the tail from the wide base
                     * rather than by subtracting from the cursor distance. */
                    const size::CellCountInt prev_x = (size::CellCountInt)(screens.active->cursor.x - prev.left);
                    if (prev_x < cols - 1) {
                        Cell *cells = prev.cell;
                        cells[1].setWide(Cell::Wide::narrow);
                    }

                    /* Place the cursor one cell after the now-narrow base,
                     * clamped to the right edge. Usually this moves the cursor
                     * back from after the old tail, but saved cursor state or
                     * changed margins can leave it directly on the base. */
                    screens.active->cursor.pending_wrap = false;
                    screens.active->cursorHorizontalAbsolute(
                        (size::CellCountInt)(prev_x + 1 < right_limit - 1 ? prev_x + 1 : right_limit - 1));
                    break;
                }

                case unicode::GraphemeWidthEffect::no_change: break;
                }

                /* log.debug("c={X} grapheme attach to left={} primary_cp={X}") */
                screens.active->cursorMarkDirty();
                if (screens.active->appendGrapheme(prev.cell, c) != PageList::IncreaseCapacityError::none)
                    return false;
                return true;
            }
            grapheme_done = true;
        } while (false);
        (void)grapheme_done;
    }

    /* Determine the width of this character so we can handle
     * non-single-width characters properly. We have a fast-path for
     * byte-sized characters since they're so common. We can ignore
     * control characters because they're always filtered prior. */
    const size_t width = c <= 0xFF ? 1 : (size_t)unicode::Table::get(c).width();

    /* Note: it is possible to have a width of "3" and a width of "-1" from
     * uucode.x's wcwidth. We should look into those cases and handle them
     * appropriately. */
    assert(width <= 2);

    /* Attach zero-width characters to our cell as grapheme data. */
    if (width == 0) {
        /* If we have grapheme clustering enabled, we don't blindly attach
         * any zero width character to our cells and we instead just ignore
         * it. */
        if (modes.get(Mode::grapheme_cluster)) return true;

        /* If we have wraparound enabled and a pending wrap, the character
         * we're attaching to is still under the cursor. Otherwise, it's the
         * cell to the left. */
        const size::CellCountInt left =
            (modes.get(Mode::wraparound) && screens.active->cursor.pending_wrap) ? 0 : 1;

        /* If we're at cell zero and not pending a wrap, then this is malformed
         * data and we don't print anything or even store this. Zero-width
         * characters are ALWAYS attached to some other non-zero-width
         * character at the time of writing. */
        if (screens.active->cursor.x == 0 && left == 1) {
            /* log.warn("zero-width character with no prior character, ignoring") */
            return true;
        }

        /* Find our previous cell */
        Cell *prev;
        {
            Cell *immediate = screens.active->cursorCellLeft(left);
            if (immediate->wide() != Cell::Wide::spacer_tail) {
                prev = immediate;
            } else {
                prev = screens.active->cursorCellLeft((size::CellCountInt)(left + 1));
            }
        }

        /* If our previous cell has no text, just ignore the zero-width character */
        if (!prev->hasText()) {
            /* log.warn("zero-width character with no prior character, ignoring") */
            return true;
        }

        /* If this is a emoji variation selector, prev must be an emoji */
        if (c == 0xFE0F || c == 0xFE0E) {
            const unicode::Properties prev_props = unicode::Table::get(prev->contentCodepoint());
            const bool emoji = prev_props.grapheme_break() == unicode::GraphemeBreakNoControl::extended_pictographic;
            if (!emoji) return true;
        }

        if (screens.active->appendGrapheme(prev, c) != PageList::IncreaseCapacityError::none) return false;
        return true;
    }

    /* We have a printable character, save it */
    previous_char = c;

    /* If we're soft-wrapping, then handle that first. */
    if (screens.active->cursor.pending_wrap && modes.get(Mode::wraparound)) {
        if (!printWrap()) return false;
    }

    /* If we have insert mode enabled then we need to handle that. We
     * only do insert mode if we're not at the end of the line. */
    if (modes.get(Mode::insert) && screens.active->cursor.x + width < cols) {
        insertBlanks(width);
    }

    switch (width) {
    /* Single cell is very easy: just write in the cell */
    case 1:
        screens.active->cursorMarkDirty();
        printCell(c, Cell::Wide::narrow);
        break;

    /* Wide character requires a spacer. We print this by
     * using two cells: the first is flagged "wide" and has the
     * wide char. The second is guaranteed to be a spacer if
     * we're not at the end of the line. */
    case 2:
        if ((right_limit - scrolling_region.left) > 1) {
            /* If we don't have space for the wide char, we need
             * to insert spacers and wrap. Then we just print the wide
             * char as normal. */
            if (screens.active->cursor.x == right_limit - 1) {
                /* If we don't have wraparound enabled then we don't print
                 * this character at all and don't move the cursor. This is
                 * how xterm behaves. */
                if (!modes.get(Mode::wraparound)) return true;

                /* We only create a spacer head if we're at the real edge
                 * of the screen. Otherwise, we clear the space with a narrow.
                 * This allows soft wrapping to work correctly. */
                if (right_limit == cols) {
                    /* Special-case: we need to set wrap to true even
                     * though we call printWrap below because if there is
                     * a page resize during printCell then it'll fail
                     * integrity checks. */
                    screens.active->cursor.page_row->setWrap(true);
                    printCell(0, Cell::Wide::spacer_head);
                } else {
                    printCell(0, Cell::Wide::narrow);
                }
                if (!printWrap()) return false;
            }

            screens.active->cursorMarkDirty();
            printCell(c, Cell::Wide::wide);
            screens.active->cursorRight(1);
            printCell(0, Cell::Wide::spacer_tail);
        } else {
            /* This is pretty broken, terminals should never be only 1-wide.
             * We should prevent this downstream. */
            screens.active->cursorMarkDirty();
            printCell(0, Cell::Wide::narrow);
        }
        break;

    default: assert(false); break;
    }

    /* If we're at the column limit, then we need to wrap the next time.
     * In this case, we don't move the cursor. */
    if (screens.active->cursor.x == right_limit - 1) {
        screens.active->cursor.pending_wrap = true;
        return true;
    }

    /* Move the cursor */
    screens.active->cursorRight(1);
    return true;
}

inline void Terminal::printCell(uint32_t unmapped_c, Cell::Wide wide) {
    struct Guard {
        Screen *s;
        ~Guard() { s->assertIntegrity(); }
    } guard = {screens.active};
    (void)guard;

    /* TODO: spacers should use a bgcolor only cell */

    uint32_t c;
    {
        /* TODO: non-utf8 handling, gr */

        /* If we're single shifting, then we use the key exactly once. */
        terminal::charsets::Slots key;
        if (screens.active->charset.single_shift.has) {
            key = screens.active->charset.single_shift.value;
            screens.active->charset.single_shift = Maybe<terminal::charsets::Slots>::none();
        } else {
            key = screens.active->charset.gl;
        }

        const terminal::charsets::Charset set = screens.active->charset.charsets.get(key);

        /* UTF-8 or ASCII is used as-is */
        if (set == terminal::charsets::Charset::utf8 || set == terminal::charsets::Charset::ascii) {
            c = unmapped_c;
        } else if (unmapped_c > 0xFF) {
            /* If we're outside of ASCII range this is an invalid value in
             * this table so we just return space. */
            c = ' ';
        } else {
            /* Get our lookup table and map it */
            const uint16_t *table = terminal::charsets::table(set);
            c = table[unmapped_c];
        }
    }

    Cell *cell = screens.active->cursor.page_cell;

    /* If the wide property of this cell is the same, then we don't
     * need to do the special handling here because the structure will
     * be the same. If it is NOT the same, then we may need to clear some
     * cells. */
    if (cell->wide() != wide) {
        switch (cell->wide()) {
        /* Previous cell was narrow. Do nothing. */
        case Cell::Wide::narrow: break;

        /* Previous cell was wide. We need to clear the tail and head. */
        case Cell::Wide::wide: {
            if (screens.active->cursor.x >= cols - 1) break;

            Cell *spacer_cell = screens.active->cursorCellRight(1);
            screens.active->clearCells(screens.active->cursor.page_pin->node->page(),
                                       screens.active->cursor.page_row, spacer_cell, 1);

            /* If we're near the left edge, a wide char may have
             * wrapped from the previous row, leaving a spacer_head
             * at the end of that row. Clear it so the previous row
             * doesn't keep a stale spacer_head. */
            if (screens.active->cursor.y > 0 && screens.active->cursor.x <= 1) {
                Cell *head_cell = screens.active->cursorCellEndOfPrev();
                if (head_cell->wide() == Cell::Wide::spacer_head) head_cell->setWide(Cell::Wide::narrow);
            }
            break;
        }

        case Cell::Wide::spacer_tail: {
            assert(screens.active->cursor.x > 0);

            /* So integrity checks pass. We fix this up later so we don't
             * need to do this without safety checks. */
            if (slow_runtime_safety) {
                cell->setWide(Cell::Wide::narrow);
            }

            Cell *wide_cell = screens.active->cursorCellLeft(1);
            screens.active->clearCells(screens.active->cursor.page_pin->node->page(),
                                       screens.active->cursor.page_row, wide_cell, 1);
            /* If we're near the left edge, a wide char may have
             * wrapped from the previous row, leaving a spacer_head
             * at the end of that row. Clear it so the previous row
             * doesn't keep a stale spacer_head. */
            if (screens.active->cursor.y > 0 && screens.active->cursor.x <= 1) {
                Cell *head_cell = screens.active->cursorCellEndOfPrev();
                if (head_cell->wide() == Cell::Wide::spacer_head) head_cell->setWide(Cell::Wide::narrow);
            }
            break;
        }

        /* TODO: this case was not handled in the old terminal implementation
         * but it feels like we should do something. investigate other
         * terminals (xterm mainly) and see what's up. */
        case Cell::Wide::spacer_head: break;
        }
    }

    /* If the prior value had graphemes, clear those */
    if (cell->hasGrapheme()) {
        Page *page = screens.active->cursor.page_pin->node->page();
        page->clearGrapheme(cell);
        page->updateRowGraphemeFlag(screens.active->cursor.page_row);
    }

    /* We don't need to update the style refs unless the
     * cell's new style will be different after writing. */
    const bool style_changed = cell->style_id() != screens.active->cursor.style_id;
    if (style_changed) {
        Page *page = screens.active->cursor.page_pin->node->page();

        /* Release the old style. */
        if (cell->style_id() != style::default_id) {
            assert(screens.active->cursor.page_row->styled());
            page->styles.release((const void *)page->memory, cell->style_id());
        }
    }

    /* Keep track if we had a hyperlink so we can unset it. */
    const bool had_hyperlink = cell->hyperlink();

    /* Write */
    {
        Cell v = Cell::init(c);
        v.setStyleId(screens.active->cursor.style_id);
        v.setWide(wide);
        v.setProtected(screens.active->cursor.protected_);
        v.setSemanticContent(screens.active->cursor.semantic_content);
        *cell = v;
    }

    if (style_changed) {
        Page *page = screens.active->cursor.page_pin->node->page();

        /* Use the new style. */
        if (cell->style_id() != style::default_id) {
            page->styles.use((const void *)page->memory, cell->style_id());
            screens.active->cursor.page_row->setStyled(true);
        }
    }

    /* We check for an active hyperlink first because setHyperlink
     * handles clearing the old hyperlink and an optimization if we're
     * overwriting the same hyperlink. */
    if (screens.active->cursor.hyperlink_id > 0) {
        if (screens.active->cursorSetHyperlink() != PageList::IncreaseCapacityError::none) {
            /* log.warn("error reallocating for more hyperlink space, ignoring hyperlink err={}")
             *
             * A partially successful grow can replace the page even when the
             * call fails, so `cell` may be stale here. The cursor pointers are
             * always reloaded, so read the cell through the cursor. */
            assert(!screens.active->cursor.page_cell->hyperlink());
        }
    } else if (had_hyperlink) {
        /* If the previous cell had a hyperlink then we need to clear it. */
        Page *page = screens.active->cursor.page_pin->node->page();
        page->clearHyperlink(cell);
        page->updateRowHyperlinkFlag(screens.active->cursor.page_row);
    }
}

inline bool Terminal::printWrap() {
    /* We only mark that we soft-wrapped if we're at the edge of our
     * full screen. We don't mark the row as wrapped if we're in the
     * middle due to a right margin. */
    Screen::Cursor *cursor_ = &screens.active->cursor;
    const bool mark_wrap = cursor_->x == cols - 1;
    if (mark_wrap) cursor_->page_row->setWrap(true);

    /* Get the old semantic prompt so we can extend it to the next
     * line. We need to do this before we index() because we may
     * modify memory. */
    const Cell::SemanticContent old_semantic = cursor_->semantic_content;
    const bool old_semantic_clear = cursor_->semantic_content_clear_eol;

    /* Move to the next line */
    if (!index()) return false;
    screens.active->cursorHorizontalAbsolute(scrolling_region.left);

    /* Our pointer should never move */
    assert(cursor_ == &screens.active->cursor);

    /* We always reset our semantic prompt state */
    cursor_->semantic_content = old_semantic;
    cursor_->semantic_content_clear_eol = old_semantic_clear;
    switch (old_semantic) {
    case Cell::SemanticContent::output:
    case Cell::SemanticContent::input: break;
    case Cell::SemanticContent::prompt:
        cursor_->page_row->setSemanticPrompt(Row::SemanticPrompt::prompt_continuation);
        break;
    }

    if (mark_wrap) {
        Row *row = screens.active->cursor.page_row;
        /* Always mark the row as a continuation */
        row->setWrapContinuation(true);
    }

    /* Assure that our screen is consistent */
    screens.active->assertIntegrity();
    return true;
}

/* Perform a semantic prompt command.
 *
 * If there is an error, we do our best to get the terminal into
 * some coherent state, since callers typically can't handle errors
 * (since they're sending sequences via the pty). */
inline bool Terminal::semanticPrompt(const terminal::osc::semantic_prompt::Command &cmd) {
    typedef terminal::osc::semantic_prompt::Command Command;
    typedef terminal::osc::semantic_prompt::Option Option;
    typedef terminal::osc::semantic_prompt::PromptKind PromptKind;
    typedef Screen::SemanticContentSet SCS;

    switch (cmd.action) {
    case Command::Action::fresh_line:
        if (!semanticPromptFreshLine()) return false;
        break;

    case Command::Action::fresh_line_new_prompt: {
        /* "First do a fresh-line." */
        if (!semanticPromptFreshLine()) return false;

        Screen *screen = screens.active;

        /* "Subsequent text (until a OSC "133;B" or OSC "133;I" command)
         * is a prompt string (as if followed by OSC 133;P;k=i\007)." */
        {
            PromptKind kind;
            if (!cmd.readOption(Option::prompt_kind, &kind)) kind = PromptKind::initial;
            screen->cursorSetSemanticContent(SCS::makePrompt(kind));
        }

        /* This is a kitty-specific flag that notes that the shell
         * is NOT capable of redraw. Redraw defaults to true so this
         * usually just disables it, but either is possible. */
        {
            terminal::osc::semantic_prompt::Redraw v;
            if (cmd.readOption(Option::redraw, &v)) {
                flags.shell_redraws_prompt = v;
            }
        }

        do { /* click */
            /* Handle click_events as a priority over cl. click_events
             * is another Kitty-specific extension that converts clicks
             * within a prompt area to SGR mouse events and defers to the
             * shell to handle them. */
            terminal::osc::semantic_prompt::ClickEvents ev;
            if (cmd.readOption(Option::click_events, &ev)) {
                screen->semantic_prompt.click.tag = Screen::SemanticPrompt::SemanticClick::Kind::click_events;
                screen->semantic_prompt.click.click_events = ev;
                break;
            }

            /* If click_events was not set or disabled, fallback to `cl`. */
            terminal::osc::semantic_prompt::Click cl;
            if (cmd.readOption(Option::cl, &cl)) {
                screen->semantic_prompt.click.tag = Screen::SemanticPrompt::SemanticClick::Kind::cl;
                screen->semantic_prompt.click.cl = cl;
            }
        } while (false);

        /* The "aid" and "cl" options are also valid for this
         * command but we don't yet handle these in any meaningful way. */
        break;
    }

    case Command::Action::new_command: {
        /* Spec:
         * Same as OSC "133;A" but may first implicitly terminate a
         * previous command: if the options specify an aid and there
         * is an active (open) command with matching aid, finish the
         * innermost such command (as well as any other commands
         * nested more deeply). If no aid is specified, treat as an
         * aid whose value is the empty string.
         *
         * Ghostty:
         * We don't currently do explicit command tracking in any way
         * so there is no need to terminate prior commands. We just
         * perform the `A` action. */
        Command next = Command::init(Command::Action::fresh_line_new_prompt);
        next.options_unvalidated = cmd.options_unvalidated;
        return semanticPrompt(next);
    }

    case Command::Action::prompt_start: {
        /* Explicit start of prompt. Optional after an A or N command.
         * The k (kind) option specifies the type of prompt:
         * regular primary prompt (k=i or default),
         * right-side prompts (k=r), or prompts for continuation lines (k=c or k=s). */
        PromptKind kind;
        if (!cmd.readOption(Option::prompt_kind, &kind)) kind = PromptKind::initial;
        screens.active->cursorSetSemanticContent(SCS::makePrompt(kind));
        break;
    }

    case Command::Action::end_prompt_start_input:
        /* End of prompt and start of user input, terminated by a OSC
         * "133;C" or another prompt (OSC "133;P"). */
        screens.active->cursorSetSemanticContent(SCS::makeInput(SCS::InputClear::clear_explicit));
        break;

    case Command::Action::end_prompt_start_input_terminate_eol:
        /* End of prompt and start of user input, terminated by end-of-line. */
        screens.active->cursorSetSemanticContent(SCS::makeInput(SCS::InputClear::clear_eol));
        break;

    case Command::Action::end_input_start_output:
        /* "End of input, and start of output." */
        screens.active->cursorSetSemanticContent(SCS::makeOutput());

        /* If our current row is marked as a prompt and we're
         * at column zero then we assume we're un-prompting. This
         * is a heuristic to deal with fish, mostly. The issue that
         * fish brings up is that it has no PS2 equivalent and its
         * builtin OSC133 marking doesn't output continuation lines
         * as k=s. So, we assume when we get a newline with a prompt
         * cursor that the new line is also a prompt. But fish changes
         * to output on the newline. So if we're at col 0 we just assume
         * we're overwriting the prompt. */
        if (screens.active->cursor.page_row->semantic_prompt() != Row::SemanticPrompt::none &&
            screens.active->cursor.x == 0) {
            screens.active->cursor.page_row->setSemanticPrompt(Row::SemanticPrompt::none);
        }
        break;

    case Command::Action::end_command:
        /* From a terminal state perspective, this doesn't really do
         * anything. Other terminals appear to do nothing here. I think
         * its reasonable at this point to reset our semantic content
         * state but the spec doesn't really say what to do. */
        screens.active->cursorSetSemanticContent(SCS::makeOutput());
        break;
    }
    return true;
}

/* OSC 133;L */
inline bool Terminal::semanticPromptFreshLine() {
    const size::CellCountInt left_margin =
        screens.active->cursor.x < scrolling_region.left ? (size::CellCountInt)0 : scrolling_region.left;

    /* Spec: "If the cursor is the initial column (left, assuming
     * left-to-right writing), do nothing" This specification is very under
     * specified. We are taking the liberty to assume that in a left/right
     * margin context, if the cursor is outside of the left margin, we treat
     * it as being at the left margin for the purposes of this command.
     * This is arbitrary. If someone has a better reasonable idea we can
     * apply it. */
    if (screens.active->cursor.x == left_margin) return true;

    carriageReturn();
    return index();
}

/* Returns true if the cursor is currently at a prompt. Another way to look
 * at this is it returns false if the shell is currently outputting something.
 * This requires shell integration (semantic prompt integration).
 *
 * If the shell integration doesn't exist, this will always return false. */
inline bool Terminal::cursorIsAtPrompt() {
    /* If we're on the secondary screen, we're never at a prompt. */
    if (screens.active_key == ScreenSet::Key::alternate) return false;

    /* If our page row is a prompt then we're always at a prompt */
    const Screen::Cursor *cursor_ = &screens.active->cursor;
    if (cursor_->page_row->semantic_prompt() != Row::SemanticPrompt::none) return true;

    /* Otherwise, determine our cursor state */
    switch (cursor_->semantic_content) {
    case Cell::SemanticContent::input:
    case Cell::SemanticContent::prompt: return true;
    default: return false;
    }
}

/* Horizontal tab moves the cursor to the next tabstop, clearing
 * the screen to the left the tabstop. */
inline void Terminal::horizontalTab() {
    while (screens.active->cursor.x < scrolling_region.right) {
        /* Move the cursor right */
        screens.active->cursorRight(1);

        /* If the last cursor position was a tabstop we return. We do
         * "last cursor position" because we want a space to be written
         * at the tabstop unless we're at the end (the while condition). */
        if (tabstops.get(screens.active->cursor.x)) return;
    }
}

/* Same as horizontalTab but moves to the previous tabstop instead of the next. */
inline void Terminal::horizontalTabBack() {
    /* With origin mode enabled, our leftmost limit is the left margin. */
    const size::CellCountInt left_limit =
        modes.get(terminal::modes::Mode::origin) ? scrolling_region.left : (size::CellCountInt)0;

    for (;;) {
        /* If we're already at the edge of the screen, then we're done. */
        if (screens.active->cursor.x <= left_limit) return;

        /* Move the cursor left */
        screens.active->cursorLeft(1);
        if (tabstops.get(screens.active->cursor.x)) return;
    }
}

/* Clear tab stops. */
inline void Terminal::tabClear(terminal::csi::TabClear cmd) {
    switch (cmd) {
    case terminal::csi::TabClear::current: tabstops.unset(screens.active->cursor.x); break;
    case terminal::csi::TabClear::all: tabstops.reset(0); break;
    default: /* log.warn("invalid or unknown tab clear setting: {}") */ break;
    }
}

/* Set a tab stop on the current cursor.
 * TODO: test */
inline void Terminal::tabSet() { tabstops.set(screens.active->cursor.x); }

/* TODO: test */
inline void Terminal::tabReset() { tabstops.reset(TABSTOP_INTERVAL); }

/* Move the cursor to the next line in the scrolling region, possibly scrolling.
 *
 * If the cursor is outside of the scrolling region: move the cursor one line
 * down if it is not on the bottom-most line of the screen.
 *
 * If the cursor is inside the scrolling region:
 *   If the cursor is on the bottom-most line of the scrolling region:
 *     invoke scroll up with amount=1
 *   If the cursor is not on the bottom-most line of the scrolling region:
 *     move the cursor one line down
 *
 * This unsets the pending wrap state without wrapping. */
inline bool Terminal::index() {
    Screen *screen = screens.active;

    /* Unset pending wrap state */
    screen->cursor.pending_wrap = false;

    /* We handle our cursor semantic prompt state AFTER doing the
     * scrolling, because we may need to apply to new rows. */
    struct SemanticGuard {
        Screen *screen;
        ~SemanticGuard() {
            if (screen->cursor.semantic_content != Cell::SemanticContent::output) {
                /* Always reset any semantic content clear-eol state.
                 *
                 * The specification is not clear what "end-of-line" means. If we
                 * discover that there are more scenarios we should be unsetting
                 * this we should document and test it. */
                if (screen->cursor.semantic_content_clear_eol) {
                    screen->cursor.semantic_content = Cell::SemanticContent::output;
                    screen->cursor.semantic_content_clear_eol = false;
                } else {
                    /* If we aren't clearing our state at EOL and we're not output,
                     * then we mark the new row as a prompt continuation. This is
                     * to work around shells that don't send OSC 133 k=s sequences
                     * for continuations.
                     *
                     * This can be a false positive if the shell changes content
                     * type later and outputs something. We handle that in the
                     * semanticPrompt function. */
                    screen->cursor.page_row->setSemanticPrompt(Row::SemanticPrompt::prompt_continuation);
                }
            } else {
                /* This should never be set in the output mode. */
                assert(!screen->cursor.semantic_content_clear_eol);
            }
        }
    } semantic_guard = {screen};
    (void)semantic_guard;

    /* Outside of the scroll region we move the cursor one line down. */
    if (screen->cursor.y < scrolling_region.top || screen->cursor.y > scrolling_region.bottom) {
        /* We only move down if we're not already at the bottom of
         * the screen. */
        if (screen->cursor.y < rows - 1) {
            screen->cursorDown(1);
        }

        return true;
    }

    /* If the cursor is inside the scrolling region and on the bottom-most
     * line, then we scroll up. If our scrolling region is the full screen
     * we create scrollback. */
    if (screen->cursor.y == scrolling_region.bottom && screen->cursor.x >= scrolling_region.left &&
        screen->cursor.x <= scrolling_region.right) {
        /* If our scrolling region is at the top, we create scrollback,
         * but only if our screen retains scrollback. If our screen
         * doesn't retain scrollback (e.g. the alternate screen) then
         * creating scrollback is pure overhead: the rows are never
         * visible and are simply pruned later. In that case we use the
         * in-place region scroll below, unless the region is a single
         * row (a one row screen) which cursorScrollRegionUp can't
         * handle (and cursorDownScroll special-cases). */
        if (scrolling_region.top == 0 && scrolling_region.left == 0 && scrolling_region.right == cols - 1 &&
            (!screen->no_scrollback || scrolling_region.bottom == 0)) {
            return screen->cursorScrollAbove();
        }

        /* Slow path for left and right scrolling region margins.
         * scrollUp handles the kitty image adjustment itself. */
        if (scrolling_region.left != 0 || scrolling_region.right != cols - 1) {
            return scrollUp(1);
        }

        /* Otherwise use a fast path function to efficiently scroll
         * the contents of the scrolling region. */
        return screen->cursorScrollRegionUp((size_t)(scrolling_region.bottom - scrolling_region.top));
    }

    /* Increase cursor by 1, maximum to bottom of scroll region */
    if (screen->cursor.y < scrolling_region.bottom) {
        screen->cursorDown(1);
    }
    return true;
}

/* Move the cursor to the previous line in the scrolling region, possibly
 * scrolling.
 *
 * If the cursor is outside of the scrolling region, move the cursor one
 * line up if it is not on the top-most line of the screen.
 *
 * If the cursor is inside the scrolling region:
 *
 *   * If the cursor is on the top-most line of the scrolling region:
 *     invoke scroll down with amount=1
 *   * If the cursor is not on the top-most line of the scrolling region:
 *     move the cursor one line up */
inline void Terminal::reverseIndex() {
    if (screens.active->cursor.y != scrolling_region.top || screens.active->cursor.x < scrolling_region.left ||
        screens.active->cursor.x > scrolling_region.right) {
        cursorUp(1);
        return;
    }

    scrollDown(1);
}

/* Set Cursor Position. Move cursor to the position indicated
 * by row and column (1-indexed). If column is 0, it is adjusted to 1.
 * If column is greater than the right-most column it is adjusted to
 * the right-most column. If row is 0, it is adjusted to 1. If row is
 * greater than the bottom-most row it is adjusted to the bottom-most
 * row. */
inline void Terminal::setCursorPos(size_t row_req, size_t col_req) {
    /* If cursor origin mode is set the cursor row will be moved relative to
     * the top margin row and adjusted to be above or at bottom-most row in
     * the current scroll region.
     *
     * If origin mode is set and left and right margin mode is set the cursor
     * will be moved relative to the left margin column and adjusted to be on
     * or left of the right margin column. */
    struct Params {
        size::CellCountInt x_offset;
        size::CellCountInt y_offset;
        size::CellCountInt x_max;
        size::CellCountInt y_max;
    } params;
    if (modes.get(terminal::modes::Mode::origin)) {
        params.x_offset = scrolling_region.left;
        params.y_offset = scrolling_region.top;
        params.x_max = (size::CellCountInt)(scrolling_region.right + 1); /* We need this 1-indexed */
        params.y_max = (size::CellCountInt)(scrolling_region.bottom + 1); /* We need this 1-indexed */
    } else {
        params.x_offset = 0;
        params.y_offset = 0;
        params.x_max = cols;
        params.y_max = rows;
    }

    /* Unset pending wrap state */
    screens.active->cursor.pending_wrap = false;

    /* Calculate our new x/y. Wisp: +| and -| are saturating. */
    const size_t row = row_req == 0 ? 1 : row_req;
    const size_t col = col_req == 0 ? 1 : col_req;
    const size_t col_off = col + params.x_offset < col ? SIZE_MAX : col + params.x_offset;
    const size_t row_off = row + params.y_offset < row ? SIZE_MAX : row + params.y_offset;
    const size_t x_sat = (size_t)params.x_max < col_off ? (size_t)params.x_max : col_off;
    const size_t y_sat = (size_t)params.y_max < row_off ? (size_t)params.y_max : row_off;
    const size::CellCountInt x = (size::CellCountInt)(x_sat > 0 ? x_sat - 1 : 0);
    const size::CellCountInt y = (size::CellCountInt)(y_sat > 0 ? y_sat - 1 : 0);

    /* If the y is unchanged then this is fast pointer math */
    if (y == screens.active->cursor.y) {
        if (x > screens.active->cursor.x) {
            screens.active->cursorRight((size::CellCountInt)(x - screens.active->cursor.x));
        } else {
            screens.active->cursorLeft((size::CellCountInt)(screens.active->cursor.x - x));
        }

        return;
    }

    /* If everything changed we do an absolute change which is slightly slower */
    screens.active->cursorAbsolute(x, y);
}

/* Set Top and Bottom Margins If bottom is not specified, 0 or bigger than
 * the number of the bottom-most row, it is adjusted to the number of the
 * bottom most row.
 *
 * If top < bottom set the top and bottom row of the scroll region according
 * to top and bottom and move the cursor to the top-left cell of the display
 * (when in cursor origin mode is set to the top-left cell of the scroll region).
 *
 * Otherwise: Set the top and bottom row of the scroll region to the top-most
 * and bottom-most line of the screen.
 *
 * Top and bottom are 1-indexed. */
inline void Terminal::setTopAndBottomMargin(size_t top_req, size_t bottom_req) {
    const size_t top = top_req > 1 ? top_req : 1;
    const size_t bottom_in = bottom_req == 0 ? (size_t)rows : bottom_req;
    const size_t bottom = (size_t)rows < bottom_in ? (size_t)rows : bottom_in;
    if (top >= bottom) return;

    scrolling_region.top = (size::CellCountInt)(top - 1);
    scrolling_region.bottom = (size::CellCountInt)(bottom - 1);
    setCursorPos(1, 1);
}

/* DECSLRM */
inline void Terminal::setLeftAndRightMargin(size_t left_req, size_t right_req) {
    /* We must have this mode enabled to do anything */
    if (!modes.get(terminal::modes::Mode::enable_left_and_right_margin)) return;

    const size_t left = left_req > 1 ? left_req : 1;
    const size_t right_in = right_req == 0 ? (size_t)cols : right_req;
    const size_t right = (size_t)cols < right_in ? (size_t)cols : right_in;
    if (left >= right) return;

    scrolling_region.left = (size::CellCountInt)(left - 1);
    scrolling_region.right = (size::CellCountInt)(right - 1);
    setCursorPos(1, 1);
}

/* Scroll the text down by one row. */
inline void Terminal::scrollDown(size_t count) {
    /* Preserve our x/y to restore. */
    struct Restore {
        Terminal *t;
        size::CellCountInt old_x;
        size::CellCountInt old_y;
        bool old_wrap;
        ~Restore() {
            t->screens.active->cursorAbsolute(old_x, old_y);
            t->screens.active->cursor.pending_wrap = old_wrap;
        }
    } restore = {this, screens.active->cursor.x, screens.active->cursor.y, screens.active->cursor.pending_wrap};
    (void)restore;

    /* Move to the top of the scroll region */
    screens.active->cursorAbsolute(scrolling_region.left, scrolling_region.top);
    insertLines(count);
}

/* Removes amount lines from the top of the scroll region. The remaining lines
 * to the bottom margin are shifted up and space from the bottom margin up
 * is filled with empty lines.
 *
 * The new lines are created according to the current SGR state.
 *
 * Does not change the (absolute) cursor position. */
inline bool Terminal::scrollUp(size_t count) {
    /* Preserve our x/y to restore. */
    struct Restore {
        Terminal *t;
        size::CellCountInt old_x;
        size::CellCountInt old_y;
        bool old_wrap;
        ~Restore() {
            t->screens.active->cursorAbsolute(old_x, old_y);
            t->screens.active->cursor.pending_wrap = old_wrap;
        }
    } restore = {this, screens.active->cursor.x, screens.active->cursor.y, screens.active->cursor.pending_wrap};
    (void)restore;

    /* If our scroll region is at the top and we have no left/right
     * margins then we move the scrolled out text into the scrollback.
     *
     * If our screen doesn't retain scrollback (e.g. the alternate
     * screen) then creating scrollback is pure overhead, so we use the
     * deleteLines path below instead, unless the region is the full
     * screen where cursorScrollAbove has a specialized fast path
     * (cursorDownScroll) for scrolling without scrollback. */
    if (scrolling_region.top == 0 && scrolling_region.left == 0 && scrolling_region.right == cols - 1 &&
        (!screens.active->no_scrollback || scrolling_region.bottom == rows - 1)) {
        /* Clamp count to the scroll region height. */
        const size_t region_height = (size_t)(scrolling_region.bottom + 1);
        const size_t adjusted_count = count < region_height ? count : region_height;

        /* TODO: Create an optimized version that can scroll N times
         * This isn't critical because in most cases, scrollUp is used
         * with count=1, but it's still a big optimization opportunity.
         *
         * Move our cursor to the bottom of the scroll region so we can
         * use the cursorScrollAbove function to create scrollback */
        screens.active->cursorAbsolute(0, scrolling_region.bottom);
        for (size_t i = 0; i < adjusted_count; i++) {
            if (!screens.active->cursorScrollAbove()) return false;
        }
        return true;
    }

    /* Move to the top of the scroll region */
    screens.active->cursorAbsolute(scrolling_region.left, scrolling_region.top);
    deleteLines(count);
    return true;
}

/* Scroll the viewport of the terminal grid. */
inline void Terminal::scrollViewport(const ScrollViewport &behavior) {
    switch (behavior.tag) {
    case ScrollViewport::Tag::top: screens.active->scroll(PageList::Scroll::top()); break;
    case ScrollViewport::Tag::bottom: screens.active->scroll(PageList::Scroll::active()); break;
    case ScrollViewport::Tag::delta: screens.active->scroll(PageList::Scroll::deltaRow(behavior.delta)); break;
    case ScrollViewport::Tag::row: screens.active->scroll(PageList::Scroll::rowAt(behavior.row)); break;
    }
}

/* Return the current compression activity value.
 *
 * Callers should schedule a `compress` call whenever this value changes. The
 * direction of the change has no meaning; this is an opaque change token
 * rather than a monotonic sequence exposed by Terminal.
 *
 * It is up to the terminal what it decides to compress, but currently
 * we compress cold (non-viewed, non-editable) scrollback history on
 * the primary screen.
 *
 * Note that compression requires specific system features, namely
 * the ability to retain virtual memory allocations while discarding their
 * physical memory backings. Callers must still use `compress` to determine
 * whether compression is supported on the current target. */
inline uint64_t Terminal::compressionActivity() const {
    const PageList::IncrementalCompressionState *state = &screens.all[(size_t)ScreenSet::Key::primary]->pages.page_compression;
    /* For now we don't use the extra 16 bits. */
    return (uint64_t)state->activity_serial;
}

/* Compress cold memory to save resident memory space.
 *
 * Full compression does a full pass compressing everything it can before
 * returning. This is not recommended for interactive terminals because
 * compression is relatively slow and with large scrollbacks this can cause
 * stalls.
 *
 * Incremental compression bounds itself on how much data it can look
 * up to compress and how much compression work it does before returning.
 * It is stateful (we maintain the state) and the return value tells callers
 * whether they should continue calling it in the future.
 *
 * Callers should schedule compression when it doesn't impact user
 * experience, for example during idle times. */
inline Terminal::CompressionResult Terminal::compress(CompressionMode mode) {
    PageList *pages = &screens.get(ScreenSet::Key::primary)->pages;
    const PageList::IncrementalCompressionResult result = mode == CompressionMode::incremental
                                                              ? pages->compress(PageList::CompressMode::incremental)
                                                              : pages->compress(PageList::CompressMode::full);

    switch (result) {
    case PageList::IncrementalCompressionResult::unsupported: return CompressionResult::unsupported;
    case PageList::IncrementalCompressionResult::pending: return CompressionResult::pending;
    default: return CompressionResult::complete;
    }
}

/* To be called before shifting a row (as in insertLines and deleteLines)
 *
 * Takes care of boundary conditions such as potentially split wide chars
 * across scrolling region boundaries and orphaned spacer heads at line
 * ends. */
inline void Terminal::rowWillBeShifted(Page *page, Row *row) {
    Cell *cells = row->cells.ptr(page->memory);

    /* If our scrolling region includes the rightmost column then we
     * need to turn any spacer heads in to normal empty cells, since
     * once we move them they no longer correspond with soft-wrapped
     * wide characters.
     *
     * If it contains either of the 2 leftmost columns, then the wide
     * characters in the first column which may be associated with a
     * spacer head will be either moved or cleared, so we also need
     * to turn the spacer heads in to empty cells in that case. */
    if (scrolling_region.right == cols - 1 || scrolling_region.left < 2) {
        Cell *end_cell = &cells[page->size.cols - 1];
        if (end_cell->wide() == Cell::Wide::spacer_head) {
            end_cell->setWide(Cell::Wide::narrow);
        }
    }

    /* If the leftmost or rightmost cells of our scrolling region
     * are parts of wide chars, we need to clear the cells' contents
     * since they'd be split by the move. */
    Cell *left_cell = &cells[scrolling_region.left];
    Cell *right_cell = &cells[scrolling_region.right];

    if (left_cell->wide() == Cell::Wide::spacer_tail) {
        Cell *wide_cell = &cells[scrolling_region.left - 1];
        if (wide_cell->hasGrapheme()) {
            page->clearGrapheme(wide_cell);
            page->updateRowGraphemeFlag(row);
        }
        wide_cell->setContentCodepoint(0);
        wide_cell->setWide(Cell::Wide::narrow);
        left_cell->setWide(Cell::Wide::narrow);
    }

    if (right_cell->wide() == Cell::Wide::wide) {
        Cell *tail_cell = &cells[scrolling_region.right + 1];
        if (right_cell->hasGrapheme()) {
            page->clearGrapheme(right_cell);
            page->updateRowGraphemeFlag(row);
        }
        right_cell->setContentCodepoint(0);
        right_cell->setWide(Cell::Wide::narrow);
        tail_cell->setWide(Cell::Wide::narrow);
    }
}

/* Renew every live page generation in an inclusive range before a full-width
 * line operation moves logical rows between their coordinates. */
inline void Terminal::invalidateFullWidthRowRange(PageList::Node *first, PageList::Node *last) {
    PageList::Node *node = first;
    for (;;) {
        /* Full-width line movement remaps cached row coordinates in this page. */
        screens.active->pages.invalidateNodeLayout(node);
        if (node == last) break;
        node = node->next;
    }
}

/* TODO(qwerasd): `insertLines` and `deleteLines` are 99% identical,
 * the majority of their logic can (and should) be abstracted in to
 * a single shared helper function, probably on `Screen` not here.
 * I'm just too lazy to do that rn :p */

/* Insert amount lines at the current cursor row. The contents of the line
 * at the current cursor row and below (to the bottom-most line in the
 * scrolling region) are shifted down by amount lines. The contents of the
 * amount bottom-most lines in the scroll region are lost.
 *
 * This unsets the pending wrap state without wrapping. If the current cursor
 * position is outside of the current scroll region it does nothing.
 *
 * If amount is greater than the remaining number of lines in the scrolling
 * region it is adjusted down (still allowing for scrolling out every remaining
 * line in the scrolling region)
 *
 * In left and right margin mode the margins are respected; lines are only
 * scrolled in the scroll region.
 *
 * All cleared space is colored according to the current SGR state.
 *
 * Moves the cursor to the left margin. */
inline void Terminal::insertLines(size_t count) {
    /* Rare, but happens */
    if (count == 0) return;

    /* If the cursor is outside the scroll region we do nothing. */
    if (screens.active->cursor.y < scrolling_region.top || screens.active->cursor.y > scrolling_region.bottom ||
        screens.active->cursor.x < scrolling_region.left || screens.active->cursor.x > scrolling_region.right)
        return;

    /* At the end we need to return the cursor to the row it started on. */
    struct Restore {
        Terminal *t;
        size::CellCountInt start_y;
        ~Restore() {
            t->screens.active->cursorAbsolute(t->scrolling_region.left, start_y);

            /* Always unset pending wrap */
            t->screens.active->cursor.pending_wrap = false;
        }
    } restore = {this, screens.active->cursor.y};
    (void)restore;

    /* We have a slower path if we have left or right scroll margins. */
    const bool left_right = scrolling_region.left > 0 || scrolling_region.right < cols - 1;

    /* Remaining rows from our cursor to the bottom of the scroll region. */
    const size_t rem = (size_t)(scrolling_region.bottom - screens.active->cursor.y + 1);

    /* We can only insert lines up to our remaining lines in the scroll
     * region. So we take whichever is smaller. */
    const size_t adjusted_count = count < rem ? count : rem;

    /* Create a new tracked pin which we'll use to navigate the page list
     * so that if we need to adjust capacity it will be properly tracked. */
    Pin *cur_p = screens.active->pages.trackPin(
        screens.active->cursor.page_pin->down((size::CellCountInt)(rem - 1)).value);
    if (!cur_p) {
        /* This error scenario means that our GPA is OOM. This is not a
         * situation we can gracefully handle. We can't just ignore insertLines
         * because it'll result in a corrupted screen. Ideally in the future
         * we flag the state as broken and show an error message to the user.
         * For now, we panic.
         * log.err("insertLines trackPin error err={}") */
        abort();
    }
    struct Untrack {
        PageList *pl;
        Pin *p;
        ~Untrack() { pl->untrackPin(p); }
    } untrack = {&screens.active->pages, cur_p};
    (void)untrack;

    /* Partial-width margins edit cells in stable rows; full-width moves rows. */
    if (!left_right) invalidateFullWidthRowRange(screens.active->cursor.page_pin->node, cur_p->node);

    /* Our current y position relative to the cursor */
    size_t y = rem;

    /* Traverse from the bottom up */
    while (y > 0) {
        const Page::RowAndCell cur_rac = cur_p->rowAndCell();
        Row *cur_row = cur_rac.row;

        /* If this is one of the lines we need to shift, do so */
        if (y > adjusted_count) {
            const Pin off_p = cur_p->up((size::CellCountInt)adjusted_count).value;
            const Page::RowAndCell off_rac = off_p.rowAndCell();
            Row *off_row = off_rac.row;

            rowWillBeShifted(cur_p->node->page(), cur_row);
            rowWillBeShifted(off_p.node->page(), off_row);

            /* If our scrolling region is full width, then we unset wrap. */
            if (!left_right) {
                off_row->setWrap(false);
                cur_row->setWrap(false);
                off_row->setWrapContinuation(false);
                cur_row->setWrapContinuation(false);
            }

            const Pin src_p = off_p;
            Row *src_row = off_row;
            const Pin dst_p = *cur_p;
            Row *dst_row = cur_row;

            /* If our page doesn't match, then we need to do a copy from
             * one page to another. This is the slow path. */
            if (src_p.node != dst_p.node) {
                /* The copy may replace the destination node in order
                 * to increase its capacity. Our pins are tracked so
                 * they update automatically; we can discard the
                 * replacement because the remainder of this iteration
                 * only accesses rows through the pins. */
                (void)screens.active->clonePartialRowGrowCapacity(dst_p.node, dst_p.y, src_p.node->page(), src_row,
                                                                  scrolling_region.left,
                                                                  (size_t)(scrolling_region.right + 1));
            } else {
                if (!left_right) {
                    /* Swap the src/dst cells. This ensures that our dst gets the
                     * proper shifted rows and src gets non-garbage cell data that
                     * we can clear. */
                    const Row dst = *dst_row;
                    *dst_row = *src_row;
                    *src_row = dst;

                    /* Ensure what we did didn't corrupt the page */
                    cur_p->node->page()->assertIntegrity();
                } else {
                    /* Left/right scroll margins we have to
                     * copy cells, which is much slower... */
                    Page *page = cur_p->node->page();
                    page->moveCells(src_row, scrolling_region.left, dst_row, scrolling_region.left,
                                    (size_t)((scrolling_region.right - scrolling_region.left) + 1));
                }
            }
        } else {
            /* Clear the cells for this row, it has been shifted. */
            rowWillBeShifted(cur_p->node->page(), cur_row);
            Page *page = cur_p->node->page();
            Cell *cells = page->getCells(cur_row);
            screens.active->clearCells(page, cur_row, cells + scrolling_region.left,
                                       (size_t)(scrolling_region.right + 1 - scrolling_region.left));

            /* With a full-width scroll region the entire row is a
             * fresh blank row: reset the metadata so nothing (wrap
             * state, semantic prompt) is retained from the row whose
             * storage it recycles. With left/right margins the row
             * keeps content outside the margins so the metadata is
             * preserved, matching the shift case above. */
            if (!left_right) cur_row->reset();
        }

        /* Mark the row as dirty */
        cur_p->markDirty();

        /* We have successfully processed a line. */
        y -= 1;
        /* Move our pin up to the next row. */
        const Maybe<Pin> p = cur_p->up(1);
        if (p.has) *cur_p = p.value;
    }
}

/* Removes amount lines from the current cursor row down. The remaining lines
 * to the bottom margin are shifted up and space from the bottom margin up is
 * filled with empty lines.
 *
 * If the current cursor position is outside of the current scroll region it
 * does nothing. If amount is greater than the remaining number of lines in the
 * scrolling region it is adjusted down.
 *
 * In left and right margin mode the margins are respected; lines are only
 * scrolled in the scroll region.
 *
 * If the cell movement splits a multi cell character that character cleared,
 * by replacing it by spaces, keeping its current attributes. All other
 * cleared space is colored according to the current SGR state.
 *
 * Moves the cursor to the left margin. */
inline void Terminal::deleteLines(size_t count) {
    /* Rare, but happens */
    if (count == 0) return;

    /* If the cursor is outside the scroll region we do nothing. */
    if (screens.active->cursor.y < scrolling_region.top || screens.active->cursor.y > scrolling_region.bottom ||
        screens.active->cursor.x < scrolling_region.left || screens.active->cursor.x > scrolling_region.right)
        return;

    /* At the end we need to return the cursor to the row it started on. */
    struct Restore {
        Terminal *t;
        size::CellCountInt start_y;
        ~Restore() {
            t->screens.active->cursorAbsolute(t->scrolling_region.left, start_y);
            /* Always unset pending wrap */
            t->screens.active->cursor.pending_wrap = false;
        }
    } restore = {this, screens.active->cursor.y};
    (void)restore;

    /* We have a slower path if we have left or right scroll margins. */
    const bool left_right = scrolling_region.left > 0 || scrolling_region.right < cols - 1;

    /* Remaining rows from our cursor to the bottom of the scroll region. */
    const size_t rem = (size_t)(scrolling_region.bottom - screens.active->cursor.y + 1);

    /* We can only insert lines up to our remaining lines in the scroll
     * region. So we take whichever is smaller. */
    const size_t adjusted_count = count < rem ? count : rem;

    /* Create a new tracked pin which we'll use to navigate the page list
     * so that if we need to adjust capacity it will be properly tracked. */
    Pin *cur_p = screens.active->pages.trackPin(*screens.active->cursor.page_pin);
    if (!cur_p) {
        /* See insertLines
         * log.err("deleteLines trackPin error err={}") */
        abort();
    }
    struct Untrack {
        PageList *pl;
        Pin *p;
        ~Untrack() { pl->untrackPin(p); }
    } untrack = {&screens.active->pages, cur_p};
    (void)untrack;

    /* Partial-width margins edit cells in stable rows; full-width moves rows. */
    if (!left_right)
        invalidateFullWidthRowRange(cur_p->node, cur_p->down((size::CellCountInt)(rem - 1)).value.node);

    /* Our current y position relative to the cursor */
    size_t y = 0;

    /* Traverse from the top down */
    while (y < rem) {
        const Page::RowAndCell cur_rac = cur_p->rowAndCell();
        Row *cur_row = cur_rac.row;

        /* If this is one of the lines we need to shift, do so */
        if (y < rem - adjusted_count) {
            const Pin off_p = cur_p->down((size::CellCountInt)adjusted_count).value;
            const Page::RowAndCell off_rac = off_p.rowAndCell();
            Row *off_row = off_rac.row;

            rowWillBeShifted(cur_p->node->page(), cur_row);
            rowWillBeShifted(off_p.node->page(), off_row);

            /* If our scrolling region is full width, then we unset wrap. */
            if (!left_right) {
                off_row->setWrap(false);
                cur_row->setWrap(false);
                off_row->setWrapContinuation(false);
                cur_row->setWrapContinuation(false);
            }

            const Pin src_p = off_p;
            Row *src_row = off_row;
            const Pin dst_p = *cur_p;
            Row *dst_row = cur_row;

            /* If our page doesn't match, then we need to do a copy from
             * one page to another. This is the slow path. */
            if (src_p.node != dst_p.node) {
                /* The copy may replace the destination node in order
                 * to increase its capacity. Our pins are tracked so
                 * they update automatically; we can discard the
                 * replacement because the remainder of this iteration
                 * only accesses rows through the pins. */
                (void)screens.active->clonePartialRowGrowCapacity(dst_p.node, dst_p.y, src_p.node->page(), src_row,
                                                                  scrolling_region.left,
                                                                  (size_t)(scrolling_region.right + 1));
            } else {
                if (!left_right) {
                    /* Swap the src/dst cells. This ensures that our dst gets the
                     * proper shifted rows and src gets non-garbage cell data that
                     * we can clear. */
                    const Row dst = *dst_row;
                    *dst_row = *src_row;
                    *src_row = dst;

                    /* Ensure what we did didn't corrupt the page */
                    cur_p->node->page()->assertIntegrity();
                } else {
                    /* Left/right scroll margins we have to
                     * copy cells, which is much slower... */
                    Page *page = cur_p->node->page();
                    page->moveCells(src_row, scrolling_region.left, dst_row, scrolling_region.left,
                                    (size_t)((scrolling_region.right - scrolling_region.left) + 1));
                }
            }
        } else {
            /* Clear the cells for this row, it's from out of bounds. */
            rowWillBeShifted(cur_p->node->page(), cur_row);
            Page *page = cur_p->node->page();
            Cell *cells = page->getCells(cur_row);
            screens.active->clearCells(page, cur_row, cells + scrolling_region.left,
                                       (size_t)(scrolling_region.right + 1 - scrolling_region.left));

            /* With a full-width scroll region the entire row is a
             * fresh blank row: reset the metadata so nothing (wrap
             * state, semantic prompt) is retained from the row whose
             * storage it recycles. With left/right margins the row
             * keeps content outside the margins so the metadata is
             * preserved, matching the shift case above. */
            if (!left_right) cur_row->reset();
        }

        /* Mark the row as dirty */
        cur_p->markDirty();

        /* We have successfully processed a line. */
        y += 1;
        /* Move our pin down to the next row. */
        const Maybe<Pin> p = cur_p->down(1);
        if (p.has) *cur_p = p.value;
    }
}

/* Inserts spaces at current cursor position moving existing cell contents
 * to the right. The contents of the count right-most columns in the scroll
 * region are lost. The cursor position is not changed.
 *
 * This unsets the pending wrap state without wrapping.
 *
 * The inserted cells are colored according to the current SGR state. */
inline void Terminal::insertBlanks(size_t count) {
    /* Unset pending wrap state without wrapping. Note: this purposely
     * happens BEFORE the scroll region check below, because that's what
     * xterm does. */
    screens.active->cursor.pending_wrap = false;

    /* If we're given a zero then we do nothing. The rest of this function
     * assumes count > 0 and will crash if zero so return early. Note that
     * this shouldn't be possible with real CSI sequences because the value
     * is clamped to 1 min. */
    if (count == 0) return;

    /* If our cursor is outside the margins then do nothing. We DO reset
     * wrap state still so this must remain below the above logic. */
    if (screens.active->cursor.x < scrolling_region.left || screens.active->cursor.x > scrolling_region.right)
        return;

    /* If our count is larger than the remaining amount, we just erase right.
     * We only do this if we can erase the entire line (no right margin).
     * if (right_limit == self.cols and
     *     count > right_limit - self.screens.active.cursor.x)
     * {
     *     self.eraseLine(.right, false);
     *     return;
     * } */

    /* left is just the cursor position but as a multi-pointer */
    Cell *left = screens.active->cursor.page_cell;
    Page *page = screens.active->cursor.page_pin->node->page();

    /* If our X is a wide spacer tail then we need to erase the
     * previous cell too so we don't split a multi-cell character. */
    if (screens.active->cursor.page_cell->wide() == Cell::Wide::spacer_tail) {
        assert(screens.active->cursor.x > 0);
        screens.active->clearCells(page, screens.active->cursor.page_row, left - 1, 2);
    }

    /* Remaining cols from our cursor to the right margin. */
    const size_t rem = (size_t)(scrolling_region.right - screens.active->cursor.x + 1);

    /* If the cell at the right margin is wide, its spacer tail is
     * outside the scroll region and would be orphaned by either the
     * shift or the clear. Clean up both halves up front. */
    {
        Cell *right_cell = left + (rem - 1);
        if (right_cell->wide() == Cell::Wide::wide)
            screens.active->clearCells(page, screens.active->cursor.page_row, right_cell, 2);
    }

    /* We can only insert blanks up to our remaining cols */
    const size_t adjusted_count = count < rem ? count : rem;

    /* This is the amount of space at the right of the scroll region
     * that will NOT be blank, so we need to shift the correct cols right.
     * "scroll_amount" is the number of such cols. */
    const size_t scroll_amount = rem - adjusted_count;
    if (scroll_amount > 0) {
        page->pauseIntegrityChecks(true);

        Cell *x = left + (scroll_amount - 1);

        /* If our last cell we're shifting is wide, then we need to clear
         * it to be empty so we don't split the multi-cell char. */
        Cell *end = x;
        if (end->wide() == Cell::Wide::wide) {
            assert(end[1].wide() == Cell::Wide::spacer_tail);
            screens.active->clearCells(page, screens.active->cursor.page_row, end, 2);
        }

        /* We work backwards so we don't overwrite data. */
        for (; x >= left; x -= 1) {
            Cell *src = x;
            Cell *dst = x + adjusted_count;
            page->swapCells(src, dst);
        }

        page->pauseIntegrityChecks(false);
    }

    /* Insert blanks. The blanks preserve the background color. */
    screens.active->clearCells(page, screens.active->cursor.page_row, left, adjusted_count);

    /* Our row is always dirty */
    screens.active->cursorMarkDirty();
}

/* Removes amount characters from the current cursor position to the right.
 * The remaining characters are shifted to the left and space from the right
 * margin is filled with spaces.
 *
 * If amount is greater than the remaining number of characters in the
 * scrolling region, it is adjusted down.
 *
 * Does not change the cursor position. */
inline void Terminal::deleteChars(size_t count_req) {
    if (count_req == 0) return;

    /* If our cursor is outside the margins then do nothing. We DO reset
     * wrap state still so this must remain below the above logic. */
    if (screens.active->cursor.x < scrolling_region.left || screens.active->cursor.x > scrolling_region.right)
        return;

    /* left is just the cursor position but as a multi-pointer */
    Cell *left = screens.active->cursor.page_cell;
    Page *page = screens.active->cursor.page_pin->node->page();

    /* Remaining cols from our cursor to the right margin. */
    const size_t rem = (size_t)(scrolling_region.right - screens.active->cursor.x + 1);

    /* We can only insert blanks up to our remaining cols */
    const size_t count = count_req < rem ? count_req : rem;

    screens.active->splitCellBoundary(screens.active->cursor.x);
    screens.active->splitCellBoundary((size::CellCountInt)(screens.active->cursor.x + count));
    screens.active->splitCellBoundary((size::CellCountInt)(scrolling_region.right + 1));

    /* This is the amount of space at the right of the scroll region
     * that will NOT be blank, so we need to shift the correct cols right.
     * "scroll_amount" is the number of such cols. */
    const size_t scroll_amount = rem - count;
    Cell *x = left;
    if (scroll_amount > 0) {
        page->pauseIntegrityChecks(true);

        Cell *right = left + (scroll_amount - 1);

        for (; x <= right; x += 1) {
            Cell *src = x + count;
            Cell *dst = x;
            page->swapCells(src, dst);
        }

        page->pauseIntegrityChecks(false);
    }

    /* Insert blanks. The blanks preserve the background color. */
    screens.active->clearCells(page, screens.active->cursor.page_row, x, rem - scroll_amount);

    /* Our row's soft-wrap is always reset. */
    screens.active->cursorResetWrap();

    /* Our row is always dirty */
    screens.active->cursorMarkDirty();
}

inline void Terminal::eraseChars(size_t count_req) {
    size_t count;
    {
        const size_t remaining = (size_t)(cols - screens.active->cursor.x);
        const size_t req = count_req > 1 ? count_req : 1;
        size_t end = remaining < req ? remaining : req;

        /* If our last cell is a wide char then we need to also clear the
         * cell beyond it since we can't just split a wide char. */
        if (end != remaining) {
            const Cell *last = screens.active->cursorCellRight((size::CellCountInt)(end - 1));
            if (last->wide() == Cell::Wide::wide) end += 1;
        }

        count = end;
    }

    /* Handle any boundary conditions on the edges of the erased area.
     *
     * TODO(qwerasd): This isn't actually correct if you take in to account
     * protected modes. We need to figure out how to make `clearCells` or at
     * least `clearUnprotectedCells` handle boundary conditions... */
    screens.active->splitCellBoundary(screens.active->cursor.x);
    screens.active->splitCellBoundary((size::CellCountInt)(screens.active->cursor.x + count));

    /* Reset our row's soft-wrap. */
    screens.active->cursorResetWrap();

    /* Mark our cursor row as dirty */
    screens.active->cursorMarkDirty();

    /* Clear the cells */
    Cell *cells = screens.active->cursor.page_cell;

    /* If we never had a protection mode, then we can assume no cells
     * are protected and go with the fast path. If the last protection
     * mode was not ISO we also always ignore protection attributes. */
    if (screens.active->protected_mode != terminal::ansi::ProtectedMode::iso) {
        screens.active->clearCells(screens.active->cursor.page_pin->node->page(), screens.active->cursor.page_row,
                                   cells, count);
        return;
    }

    screens.active->clearUnprotectedCells(screens.active->cursor.page_pin->node->page(),
                                          screens.active->cursor.page_row, cells, count);
}

/* Erase the line. */
inline void Terminal::eraseLine(terminal::csi::EraseLine mode, bool protected_req) {
    typedef terminal::csi::EraseLine EraseLine;

    /* Get our start/end positions depending on mode. */
    size::CellCountInt start, end;
    switch (mode) {
    case EraseLine::right: {
        size::CellCountInt x = screens.active->cursor.x;

        /* If our X is a wide spacer tail then we need to erase the
         * previous cell too so we don't split a multi-cell character. */
        if (x > 0 && screens.active->cursor.page_cell->wide() == Cell::Wide::spacer_tail) {
            x -= 1;
        }

        /* Reset our row's soft-wrap. */
        screens.active->cursorResetWrap();

        start = x;
        end = cols;
        break;
    }

    case EraseLine::left: {
        size::CellCountInt x = screens.active->cursor.x;

        /* If our x is a wide char we need to delete the tail too. */
        if (screens.active->cursor.page_cell->wide() == Cell::Wide::wide) {
            x += 1;
        }

        start = 0;
        end = (size::CellCountInt)(x + 1);
        break;
    }

    case EraseLine::complete:
        /* Xterm preserves this flag for EL2, but it also doesn't reflow
         * rows when resizing. Since we do, the erased row must no longer
         * continue onto the next row. */
        screens.active->cursorResetWrap();

        start = 0;
        end = cols;
        break;

    default:
        /* log.err("unimplemented erase line mode: {}") */
        return;
    }

    /* All modes will clear the pending wrap state and we know we have
     * a valid mode at this point. */
    screens.active->cursor.pending_wrap = false;

    /* We always mark our row as dirty */
    screens.active->cursorMarkDirty();

    /* Start of our cells */
    Cell *cells = screens.active->cursor.page_cell - screens.active->cursor.x;

    /* We respect protected attributes if explicitly requested (probably
     * a DECSEL sequence) or if our last protected mode was ISO even if its
     * not currently set. */
    const bool protected_ = screens.active->protected_mode == terminal::ansi::ProtectedMode::iso || protected_req;

    /* If we're not respecting protected attributes, we can use a fast-path
     * to fill the entire line. */
    if (!protected_) {
        screens.active->clearCells(screens.active->cursor.page_pin->node->page(), screens.active->cursor.page_row,
                                   cells + start, (size_t)(end - start));
        return;
    }

    screens.active->clearUnprotectedCells(screens.active->cursor.page_pin->node->page(),
                                          screens.active->cursor.page_row, cells + start, (size_t)(end - start));
}

/* Erase the display. */
inline void Terminal::eraseDisplay(terminal::csi::EraseDisplay mode, bool protected_req) {
    typedef terminal::csi::EraseDisplay EraseDisplay;

    /* We respect protected attributes if explicitly requested (probably
     * a DECSEL sequence) or if our last protected mode was ISO even if its
     * not currently set. */
    const bool protected_ = screens.active->protected_mode == terminal::ansi::ProtectedMode::iso || protected_req;

    switch (mode) {
    case EraseDisplay::scroll_complete: {
        if (!screens.active->scrollClear()) {
            /* log.warn("scroll clear failed, doing a normal clear err={}") */
            eraseDisplay(EraseDisplay::complete, protected_req);
            return;
        }

        /* Unsets pending wrap state */
        screens.active->cursor.pending_wrap = false;
        break;
    }

    case EraseDisplay::complete: {
        /* If we're on the primary screen and our last non-empty row is
         * a prompt, then we do a scroll_complete instead. This is a
         * heuristic to get the generally desirable behavior that ^L
         * at a prompt scrolls the screen contents prior to clearing.
         * Most shells send `ESC [ H ESC [ 2 J` so we can't just check
         * our current cursor position. See #905 */
        if (screens.active_key == ScreenSet::Key::primary) {
            do { /* at_prompt */
                /* Go from the bottom of the active up and see if we're
                 * at a prompt. */
                const Maybe<Pin> active_br = screens.active->pages.getBottomRight(point::Tag::active);
                if (!active_br.has) break;
                PageList::RowIterator it = active_br.value.rowIterator(
                    PageList::Direction::left_up, screens.active->pages.getTopLeft(point::Tag::active));
                bool at_prompt = false;
                Pin p;
                while (it.next(&p)) {
                    const Row *row = p.rowAndCell().row;
                    const Row::SemanticPrompt sp = row->semantic_prompt();
                    /* If we're at a prompt or input area, then we are at a prompt. */
                    if (sp == Row::SemanticPrompt::prompt || sp == Row::SemanticPrompt::prompt_continuation) {
                        at_prompt = true;
                        break;
                    }
                    /* If we have command output, then we're most certainly not
                     * at a prompt. */
                    break;
                }
                if (!at_prompt) break;

                /* If we fail, we just fall back to doing a normal clear
                 * so we don't worry about the error. */
                (void)screens.active->scrollClear();
            } while (false);
        }

        /* All active area */
        screens.active->clearRows(point::Point::active(), Maybe<point::Point>(), protected_);

        /* Unsets pending wrap state */
        screens.active->cursor.pending_wrap = false;

        /* Cleared screen dirty bit */
        flags.dirty.clear = true;
        break;
    }

    case EraseDisplay::below: {
        /* All lines to the right (including the cursor) */
        eraseLine(terminal::csi::EraseLine::right, protected_req);

        /* All lines below */
        if (screens.active->cursor.y + 1 < rows) {
            screens.active->clearRows(point::Point::active(0, (uint32_t)(screens.active->cursor.y + 1)),
                                      Maybe<point::Point>(), protected_);
        }

        /* Unsets pending wrap state. Should be done by eraseLine. */
        assert(!screens.active->cursor.pending_wrap);
        break;
    }

    case EraseDisplay::above: {
        /* Erase to the left (including the cursor) */
        eraseLine(terminal::csi::EraseLine::left, protected_req);

        /* All lines above */
        if (screens.active->cursor.y > 0) {
            screens.active->clearRows(point::Point::active(0, 0),
                                      point::Point::active(0, (uint32_t)(screens.active->cursor.y - 1)), protected_);
        }

        /* Unsets pending wrap state */
        assert(!screens.active->cursor.pending_wrap);
        break;
    }

    case EraseDisplay::scrollback: screens.active->eraseHistory(Maybe<point::Point>()); break;
    }
}

/* Resets all margins and fills the whole screen with the character 'E'
 *
 * Sets the cursor to the top left corner. */
inline bool Terminal::decaln() {
    /* Clear our stylistic attributes. This is the only thing that can
     * fail so we do it first so we can undo it. */
    const style::Style old_style = screens.active->cursor.style;
    {
        style::Style s;
        s.bg_color = screens.active->cursor.style.bg_color;
        s.fg_color = screens.active->cursor.style.fg_color;
        screens.active->cursor.style = s;
    }
    if (screens.active->manualStyleUpdate() != PageList::IncreaseCapacityError::none) {
        screens.active->cursor.style = old_style;
        return false;
    }

    /* Reset margins, also sets cursor to top-left */
    scrolling_region.top = 0;
    scrolling_region.bottom = (size::CellCountInt)(rows - 1);
    scrolling_region.left = 0;
    scrolling_region.right = (size::CellCountInt)(cols - 1);

    /* Origin mode is disabled */
    modes.set(terminal::modes::Mode::origin, false);

    /* Move our cursor to the top-left */
    setCursorPos(1, 1);

    /* Use clearRows instead of eraseDisplay because we must NOT respect
     * protected attributes here. */
    screens.active->clearRows(point::Point::active(), Maybe<point::Point>(), false);

    /* Fill with Es by moving the cursor but reset it after. */
    for (;;) {
        Page *page = screens.active->cursor.page_pin->node->page();
        Row *row = screens.active->cursor.page_row;
        Cell *cells = row->cells.ptr(page->memory);
        const size_t cells_len = page->size.cols;
        {
            Cell v = Cell::init('E');
            v.setStyleId(screens.active->cursor.style_id);
            /* DECALN does not respect protected state. Verified with xterm. */
            v.setProtected(false);
            for (size_t i = 0; i < cells_len; i++) cells[i] = v;
        }

        /* If we have a ref-counted style, increase */
        if (screens.active->cursor.style_id != style::default_id) {
            page->styles.useMultiple((const void *)page->memory, screens.active->cursor.style_id,
                                     (size::CellCountInt)cells_len);
            row->setStyled(true);
        }

        /* We messed with the page so assert its integrity here. */
        page->assertIntegrity();

        screens.active->cursorMarkDirty();
        if (screens.active->cursor.y == rows - 1) break;
        screens.active->cursorDown(1);
    }

    /* Reset the cursor to the top-left */
    setCursorPos(1, 1);
    return true;
}

} /* namespace vt */
} /* namespace wisp */

#endif /* WISP_VT_TERMINAL_IMPL_HPP */
