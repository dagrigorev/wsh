/* Ported from Ghostty src/terminal/PageList.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * The list of pages that makes up a terminal's screen and its scrollback.
 *
 * A Page is a fixed allocation: it holds a set number of rows and a bounded
 * amount of style, grapheme and link storage, and it cannot be extended. That
 * is what makes a page cheap — everything inside it is addressed by offsets
 * into one block, so a page can be moved, copied or written to disk whole.
 * The cost is that a page fills up, and the last three page operations all
 * ended at the same place: when a page runs out, something has to hand out a
 * new one. This is that something.
 *
 * Pages are kept in a doubly linked list, oldest first. Text scrolling off the
 * screen does not move: it stays in the page it was written to, and the screen
 * is simply a window onto the last rows of the list. Scrollback is therefore
 * not a separate structure to copy into — it is the pages nobody is looking at
 * any more, and forgetting it is freeing the page at the front.
 *
 * PARTIAL PORT. The list itself, page allocation, growth and trimming, pins,
 * the viewport, scrolling and resizing are here. Page splitting and pin
 * tracking across a reflow are not, so the ledger records PageList.zig as
 * `wip`.
 */

#pragma once
#ifndef WISP_TERMINAL_PAGE_LIST_HPP
#define WISP_TERMINAL_PAGE_LIST_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "page.hpp"

namespace wisp {
namespace terminal {

/* ─── page allocation ────────────────────────────────────────────────────── */

/* A page's cell array wants 64-byte alignment, and malloc does not promise
 * it. The raw pointer is kept beside the aligned one so free still gets what
 * malloc returned. */
static const size_t PAGE_ALLOC_ALIGN = 64;

/* How large a page should be.
 *
 * This is a tradeoff, not a constant handed down from anywhere: larger pages
 * mean fewer allocations and fewer seams for an operation to cross, smaller
 * ones mean less waste in a page that is half empty and finer-grained
 * scrollback trimming. 64 KiB is small enough that a terminal with a short
 * scrollback is not paying for a megabyte it will never write to, and large
 * enough that an 80-column page holds several hundred rows. */
static const size_t PAGE_TARGET_BYTES = 64 * 1024;

/* One page, and its place in the list. */
struct PageNode {
    Page      page;
    PageNode *prev;
    PageNode *next;

    /* How many of the page's rows hold anything.
     *
     * A page's capacity is how many rows it *could* hold; this is how many it
     * does. They are not the same thing and treating them as one would mean a
     * fresh 500-row page claiming 500 rows of scrollback the moment it was
     * allocated, when the terminal has printed nothing. Only the last page in
     * the list is ever partly filled — that is where growth happens — so the
     * distinction costs one field and nothing else. */
    CellCountInt rows_used;

    /* What malloc returned, which is not where the page begins. */
    void *alloc;

    PageNode()
        : page(), prev(nullptr), next(nullptr), rows_used(0), alloc(nullptr) {}
};

/* How many rows a page of about PAGE_TARGET_BYTES holds at this width.
 *
 * A page has a fixed overhead — the style set, the maps, the string and
 * grapheme storage — that does not shrink with the row count, so this is
 * found by asking the layout rather than by dividing. At least min_rows are
 * always granted: a page that cannot hold the screen would be useless however
 * large the overhead turned out to be. */
inline CellCountInt page_list_rows_per_page(CellCountInt cols,
                                            CellCountInt min_rows) {
    if (cols == 0) return min_rows;

    CellCountInt best = 1;
    for (CellCountInt rows = 1; rows < 4096; rows++) {
        Capacity cap(cols, rows);
        if (PageLayout::init(cap).total_size > PAGE_TARGET_BYTES) break;
        best = rows;
    }

    return best < min_rows ? min_rows : best;
}

/* Allocate and initialize one page. Returns null if the allocation failed. */
inline PageNode *page_node_create(const Capacity &cap) {
    const size_t size = PageLayout::init(cap).total_size;

    PageNode *node = (PageNode *)malloc(sizeof(PageNode));
    if (!node) return nullptr;
    new (node) PageNode();

    void *raw = malloc(size + PAGE_ALLOC_ALIGN);
    if (!raw) {
        free(node);
        return nullptr;
    }

    /* Page::init takes a buffer it may assume is zeroed — the sets and
     * allocators inside it write nothing at startup, because all-zero already
     * means empty. */
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + (PAGE_ALLOC_ALIGN - 1)) &
                                ~(uintptr_t)(PAGE_ALLOC_ALIGN - 1));
    memset(base, 0, size);

    node->alloc = raw;
    node->page = Page::init(base, cap);
    return node;
}

inline void page_node_destroy(PageNode *node) {
    if (!node) return;
    free(node->alloc);
    free(node);
}

/* ─── pins ───────────────────────────────────────────────────────────────── */

/* A reference to a cell that survives the list changing around it.
 *
 * Row and column indices into "the screen" are not stable: a scroll renumbers
 * every row, and a resize renumbers them again. A pin names the page instead,
 * and a page's own rows do not move while it lives — which is exactly the
 * property scrolling within a page was built to preserve. Selections, the
 * cursor and the viewport are all pins upstream. */
struct Pin {
    PageNode   *node;
    CellCountInt y;
    CellCountInt x;

    Pin() : node(nullptr), y(0), x(0) {}
    Pin(PageNode *n, CellCountInt row, CellCountInt col)
        : node(n), y(row), x(col) {}

    bool valid() const { return node != nullptr; }

    Row *row() const { return node ? node->page.get_row(y) : nullptr; }
    Cell *cell() const { return node ? node->page.get_cell(x, y) : nullptr; }

    bool operator==(const Pin &o) const {
        return node == o.node && y == o.y && x == o.x;
    }

    /* Move n rows toward the end of the list, crossing page boundaries.
     * Returns false and leaves the pin alone if it would run off the end. */
    bool down(size_t n) {
        PageNode *cur = node;
        size_t cy = y;
        while (cur) {
            const size_t left = (size_t)cur->rows_used - cy - 1;
            if (n <= left) {
                node = cur;
                y = (CellCountInt)(cy + n);
                return true;
            }
            n -= left + 1;
            cur = cur->next;
            cy = 0;
        }
        return false;
    }

    /* Move n rows toward the start of the list. */
    bool up(size_t n) {
        PageNode *cur = node;
        size_t cy = y;
        while (cur) {
            if (n <= cy) {
                node = cur;
                y = (CellCountInt)(cy - n);
                return true;
            }
            n -= cy + 1;
            cur = cur->prev;
            if (!cur) return false;
            cy = (size_t)(cur->rows_used - 1);
        }
        return false;
    }
};

/* ─── the list ───────────────────────────────────────────────────────────── */

/* What the viewport is following.
 *
 * Two of these are not positions but intentions, and that is the point. A
 * viewport that merely remembered a row would have to be corrected every time
 * the list grew or was trimmed; one that remembers *what it is following*
 * needs no correction at all.
 *
 *   active — stay at the bottom. Output scrolls under it, and the user keeps
 *            seeing the newest text without anything being recomputed.
 *   top    — stay at the oldest row the list still has.
 *   pin    — stay exactly here, wherever the list moves around it. This is
 *            the one a user scrolled up by hand gets: new output arrives
 *            below without dragging the screen along. */
enum class ViewportTag : uint8_t {
    active = 0,
    top = 1,
    pin = 2,
};

struct PageList {
    PageNode *first;
    PageNode *last;

    /* Screen geometry. The rows here are how many the screen shows, not how
     * many the list holds — the list holds those plus the scrollback. */
    CellCountInt cols;
    CellCountInt rows;

    size_t page_count;
    size_t row_count;   /* rows across every page, screen and scrollback */
    size_t bytes;       /* what the pages cost, for the trimming decision */

    /* The ceiling on that cost. Zero means no limit, which is what a screen
     * with unlimited scrollback asks for. */
    size_t max_size;

    /* Where the user is looking. See the viewport section below. */
    ViewportTag viewport;
    Pin         viewport_pin;

    PageList()
        : first(nullptr), last(nullptr), cols(0), rows(0),
          page_count(0), row_count(0), bytes(0), max_size(0),
          viewport(ViewportTag::active), viewport_pin() {}
};

/* Append a page holding used_rows rows of a cap_rows page. Returns null if
 * the allocation failed; the list is unchanged in that case. */
inline PageNode *page_list_append_partial(PageList *l, CellCountInt cap_rows,
                                          CellCountInt used_rows) {
    if (used_rows > cap_rows) used_rows = cap_rows;

    Capacity cap(l->cols, cap_rows);
    PageNode *node = page_node_create(cap);
    if (!node) return nullptr;
    node->rows_used = used_rows;

    node->prev = l->last;
    if (l->last) {
        l->last->next = node;
    } else {
        l->first = node;
    }
    l->last = node;

    l->page_count++;
    l->row_count += used_rows;
    l->bytes += node->page.size;
    return node;
}

/* Append a page that is entirely in use. */
inline PageNode *page_list_append(PageList *l, CellCountInt page_rows) {
    return page_list_append_partial(l, page_rows, page_rows);
}

/* Drop the oldest page.
 *
 * This is all forgetting scrollback amounts to: the rows are not copied or
 * compacted anywhere, the page holding them is simply freed. */
inline void page_list_drop_first(PageList *l) {
    PageNode *node = l->first;
    if (!node) return;

    l->first = node->next;
    if (l->first) {
        l->first->prev = nullptr;
    } else {
        l->last = nullptr;
    }

    l->page_count--;
    l->row_count -= node->rows_used;
    l->bytes -= node->page.size;

    /* A viewport pinned into the page being freed would be left pointing at
     * memory that is gone. It falls back to the top, which is the nearest
     * thing to where it was looking that still exists. */
    if (l->viewport == ViewportTag::pin && l->viewport_pin.node == node) {
        l->viewport = ViewportTag::top;
        l->viewport_pin = Pin();
    }

    page_node_destroy(node);
}

/* Free pages from the front until the list fits under max_size.
 *
 * The screen itself is never given up: trimming stops while dropping the next
 * page would leave fewer rows than the screen shows. A max_size too small to
 * hold the screen is therefore honored as far as it can be and no further —
 * losing the rows the user is looking at would be a stranger answer than
 * exceeding the limit. */
inline void page_list_trim(PageList *l) {
    if (l->max_size == 0) return;

    while (l->bytes > l->max_size && l->first && l->first != l->last) {
        const size_t dropping = (size_t)l->first->rows_used;
        if (l->row_count - dropping < (size_t)l->rows) break;
        page_list_drop_first(l);
    }
}

/* Add one row to the end of the list, returning a pin to it.
 *
 * This is what a newline at the bottom of the screen calls, and it is the
 * only place a page ever runs out. Most calls cost nothing but an increment:
 * the last page has room, so the row it already owns is simply declared to be
 * in use. Only when that page is full does a new one get allocated, and the
 * rows before it stay exactly where they were written — which is the whole
 * reason the list exists.
 *
 * Returns an invalid pin if a new page was needed and could not be
 * allocated. The list is unchanged in that case. */
inline Pin page_list_grow(PageList *l) {
    PageNode *node = l->last;

    if (node && node->rows_used < node->page.capacity.rows) {
        const CellCountInt y = node->rows_used;
        node->rows_used++;
        l->row_count++;
        page_clear_row(&node->page, y);
        page_list_trim(l);
        return Pin(node, y, 0);
    }

    /* The last page is full, so the text stays in it and a new page takes
     * over. Nothing is copied. */
    PageNode *fresh = page_list_append_partial(
        l, page_list_rows_per_page(l->cols, l->rows), 1);
    if (!fresh) return Pin();

    page_clear_row(&fresh->page, 0);
    page_list_trim(l);
    return Pin(fresh, 0, 0);
}

/* Add n rows, returning how many were actually added. Fewer than asked for
 * means an allocation failed partway. */
inline size_t page_list_grow_rows(PageList *l, size_t n) {
    size_t done = 0;
    for (; done < n; done++) {
        if (!page_list_grow(l).valid()) break;
    }
    return done;
}

/* Build a list able to show rows rows at cols columns.
 *
 * max_size caps the total bytes of the pages; zero means unlimited. Returns
 * false if the first page could not be allocated, in which case the list is
 * left empty and needs no cleanup. */
inline bool page_list_init(PageList *l, CellCountInt cols, CellCountInt rows,
                           size_t max_size) {
    *l = PageList();
    l->cols = cols;
    l->rows = rows;
    l->max_size = max_size;

    /* One page is enough to start, sized to hold at least the screen but
     * holding only the screen's rows to begin with. A terminal that has
     * printed nothing has no scrollback, however large the page it was given
     * happens to be. */
    return page_list_append_partial(l, page_list_rows_per_page(cols, rows),
                                    rows) != nullptr;
}

inline void page_list_deinit(PageList *l) {
    PageNode *node = l->first;
    while (node) {
        PageNode *next = node->next;
        page_node_destroy(node);
        node = next;
    }
    *l = PageList();
}

/* ─── addressing ─────────────────────────────────────────────────────────── */

/* The row at an index counted from the oldest row in the list.
 *
 * Walking is fine here: the list is short — a page holds hundreds of rows, so
 * even a large scrollback is tens of nodes — and every caller that wants a
 * row repeatedly should be holding a pin instead. */
inline Pin page_list_pin(const PageList *l, size_t row_index) {
    PageNode *node = l->first;
    while (node) {
        const size_t n = (size_t)node->rows_used;
        if (row_index < n) return Pin(node, (CellCountInt)row_index, 0);
        row_index -= n;
        node = node->next;
    }
    return Pin();
}

/* The first row the screen shows: the last `rows` rows of the list. */
inline Pin page_list_active_start(const PageList *l) {
    if (l->row_count < (size_t)l->rows) return page_list_pin(l, 0);
    return page_list_pin(l, l->row_count - (size_t)l->rows);
}

/* A pin for a screen coordinate, where y is counted from the top of the
 * screen rather than from the start of the scrollback. */
inline Pin page_list_active_pin(const PageList *l, CellCountInt x,
                                CellCountInt y) {
    Pin p = page_list_active_start(l);
    if (!p.valid()) return p;
    if (y > 0 && !p.down((size_t)y)) return Pin();
    p.x = x;
    return p;
}

/* The index of a pin's row, counted from the oldest row in the list, or
 * SIZE_MAX if the pin does not belong to this list. */
inline size_t page_list_row_index(const PageList *l, const Pin &p) {
    if (!p.valid()) return (size_t)-1;

    size_t index = 0;
    for (PageNode *node = l->first; node; node = node->next) {
        if (node == p.node) return index + (size_t)p.y;
        index += (size_t)node->rows_used;
    }
    return (size_t)-1;
}

/* ─── the viewport ───────────────────────────────────────────────────────── */

/* How many rows of scrollback sit above the active area.
 *
 * This is also the largest row index the viewport can start at: scrolling
 * further down would show rows past the bottom of the list. */
inline size_t page_list_max_scroll(const PageList *l) {
    return l->row_count > (size_t)l->rows ? l->row_count - (size_t)l->rows : 0;
}

/* The first row the user is currently looking at. */
inline Pin page_list_viewport_start(const PageList *l) {
    switch (l->viewport) {
        case ViewportTag::active:
            return page_list_active_start(l);
        case ViewportTag::top:
            return page_list_pin(l, 0);
        case ViewportTag::pin:
            return l->viewport_pin;
    }
    return page_list_active_start(l);
}

/* A pin for a viewport coordinate, y counted from the top of what is shown. */
inline Pin page_list_viewport_cell(const PageList *l, CellCountInt x,
                                   CellCountInt y) {
    Pin p = page_list_viewport_start(l);
    if (!p.valid()) return p;
    if (y > 0 && !p.down((size_t)y)) return Pin();
    p.x = x;
    return p;
}

/* How far the viewport sits above the active area, in rows. Zero means the
 * user is looking at the newest output. */
inline size_t page_list_viewport_offset(const PageList *l) {
    if (l->viewport == ViewportTag::active) return 0;

    const size_t max = page_list_max_scroll(l);
    const size_t at = page_list_row_index(l, page_list_viewport_start(l));
    if (at == (size_t)-1 || at >= max) return 0;
    return max - at;
}

/* Jump to the newest output and stay there. */
inline void page_list_scroll_active(PageList *l) {
    l->viewport = ViewportTag::active;
    l->viewport_pin = Pin();
}

/* Jump to the oldest row the list still holds. */
inline void page_list_scroll_top(PageList *l) {
    l->viewport = ViewportTag::top;
    l->viewport_pin = Pin();
}

/* Look at a particular row and stay on it. */
inline void page_list_scroll_to_pin(PageList *l, const Pin &p) {
    if (!p.valid()) return;
    l->viewport = ViewportTag::pin;
    l->viewport_pin = Pin(p.node, p.y, 0);
}

/* Move the viewport by delta rows: negative goes back into the scrollback,
 * positive returns toward the newest output.
 *
 * Landing at the bottom switches back to following the active area rather
 * than pinning the row that happens to be there. That is what makes a
 * terminal behave the way people expect: scroll all the way down once and new
 * output keeps you there, instead of sliding away the moment it arrives. */
inline void page_list_scroll_delta(PageList *l, long delta) {
    const size_t max = page_list_max_scroll(l);

    size_t at = page_list_row_index(l, page_list_viewport_start(l));
    if (at == (size_t)-1) at = max;

    size_t target;
    if (delta < 0) {
        const size_t back = (size_t)(-delta);
        target = back >= at ? 0 : at - back;
    } else {
        const size_t fwd = (size_t)delta;
        target = at + fwd;
        if (target < at || target > max) target = max;   /* also catches overflow */
    }

    if (target >= max) {
        page_list_scroll_active(l);
        return;
    }
    if (target == 0) {
        page_list_scroll_top(l);
        return;
    }

    Pin p = page_list_pin(l, target);
    if (!p.valid()) {
        page_list_scroll_active(l);
        return;
    }
    page_list_scroll_to_pin(l, p);
}


/* ─── resizing ───────────────────────────────────────────────────────────── */

/* Change the screen's height without touching its contents.
 *
 * Only the size of the active area changes. A taller screen shows rows that
 * were already there, from the scrollback, without moving them; a shorter one
 * pushes rows back into the scrollback. Neither needs the text disturbed,
 * which is why this is separate from the width case below. */
inline bool page_list_resize_rows(PageList *l, CellCountInt new_rows) {
    l->rows = new_rows;

    /* A screen taller than everything written so far needs the missing rows
     * to exist before it can show them. */
    if (l->row_count < (size_t)new_rows) {
        const size_t want = (size_t)new_rows - l->row_count;
        if (page_list_grow_rows(l, want) != want) return false;
    }

    page_list_trim(l);
    return true;
}

/* Resize the screen, re-laying every line at the new width.
 *
 * Width is the hard case, and it is the reason reflow is resumable. The
 * result is built as a second list and swapped in at the end: a line is read
 * out of the source pages as a stream and written into destination pages as
 * they fill, and neither side's page boundaries line up with the other's.
 * A single logical line can start in one source page, be laid out across two
 * destination pages, and finish in a third source page — the cursor carries
 * the open line across every one of those seams.
 *
 * Building a second list rather than reflowing in place also means a failure
 * partway leaves the terminal exactly as it was. There is no half-resized
 * state to recover from, which for an operation this involved is worth more
 * than the memory it costs while both lists exist.
 *
 * The viewport is returned to the active area. Upstream tracks pins through a
 * reflow so a user scrolled up stays roughly where they were reading; that
 * needs a registry of live pins, which is not ported, and moving someone to
 * the wrong place would be worse than moving them to a place they asked for.
 *
 * Returns false and leaves the list untouched if an allocation failed. */
inline bool page_list_resize(PageList *l, CellCountInt new_cols,
                             CellCountInt new_rows) {
    if (new_cols == 0 || new_rows == 0) return false;
    if (new_cols == l->cols) return page_list_resize_rows(l, new_rows);

    PageList out;
    out.cols = new_cols;
    out.rows = new_rows;
    out.max_size = l->max_size;

    const CellCountInt cap_rows = page_list_rows_per_page(new_cols, new_rows);

    PageNode *dst = page_list_append_partial(&out, cap_rows, 0);
    if (!dst) {
        page_list_deinit(&out);
        return false;
    }

    ReflowCursor cur(0);

    for (PageNode *src = l->first; src; src = src->next) {
        CellCountInt sy = 0;
        CellCountInt sx = 0;

        while (sy < src->rows_used) {
            ReflowResult r = page_reflow_resume(
                &dst->page, &cur, &src->page, sy, sx,
                (CellCountInt)(src->rows_used - sy));

            /* The cursor is the truth about how much of the destination page
             * is now in use: it knows whether the row it is on has been
             * started, which the row count alone cannot say. */
            const CellCountInt used =
                (CellCountInt)(cur.y + (cur.row_begun ? 1 : 0));
            out.row_count += (size_t)(used - dst->rows_used);
            dst->rows_used = used;

            if (!r.ok) {
                page_list_deinit(&out);
                return false;
            }

            sy = r.src_y;
            sx = r.src_x;

            if (r.full) {
                PageNode *next = page_list_append_partial(&out, cap_rows, 0);
                if (!next) {
                    page_list_deinit(&out);
                    return false;
                }
                dst = next;

                /* A new page, but not necessarily a new line. If a line was
                 * open it stays open: the previous page's last row already
                 * carries the wrap flag, and the line continues on row 0 of
                 * this one. */
                const bool open = cur.in_line;
                cur = ReflowCursor(0);
                cur.in_line = open;
            }
        }
    }

    /* A screen taller than what was reflowed needs the rest to exist. */
    if (out.row_count < (size_t)new_rows) {
        const size_t want = (size_t)new_rows - out.row_count;
        if (page_list_grow_rows(&out, want) != want) {
            page_list_deinit(&out);
            return false;
        }
    }

    page_list_trim(&out);

    PageList old = *l;
    *l = out;
    page_list_deinit(&old);
    return true;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_PAGE_LIST_HPP */
