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
 * PARTIAL PORT. The list itself, page allocation, growth and trimming, and
 * pins are here. The viewport, scrolling between pages, reflow across a resize
 * and page splitting are not, so the ledger records PageList.zig as `wip`.
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

    /* What malloc returned, which is not where the page begins. */
    void *alloc;

    PageNode() : page(), prev(nullptr), next(nullptr), alloc(nullptr) {}
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
            const size_t left = (size_t)cur->page.capacity.rows - cy - 1;
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
            cy = (size_t)(cur->page.capacity.rows - 1);
        }
        return false;
    }
};

/* ─── the list ───────────────────────────────────────────────────────────── */

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

    PageList()
        : first(nullptr), last(nullptr), cols(0), rows(0),
          page_count(0), row_count(0), bytes(0), max_size(0) {}
};

/* Append a page to the end of the list. Returns null if the allocation
 * failed; the list is unchanged in that case. */
inline PageNode *page_list_append(PageList *l, CellCountInt page_rows) {
    Capacity cap(l->cols, page_rows);
    PageNode *node = page_node_create(cap);
    if (!node) return nullptr;

    node->prev = l->last;
    if (l->last) {
        l->last->next = node;
    } else {
        l->first = node;
    }
    l->last = node;

    l->page_count++;
    l->row_count += page_rows;
    l->bytes += node->page.size;
    return node;
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
    l->row_count -= node->page.capacity.rows;
    l->bytes -= node->page.size;
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
        const size_t dropping = (size_t)l->first->page.capacity.rows;
        if (l->row_count - dropping < (size_t)l->rows) break;
        page_list_drop_first(l);
    }
}

/* Add a page at the end, trimming the front if that puts the list over its
 * limit. This is what a page filling up calls. */
inline PageNode *page_list_grow(PageList *l) {
    PageNode *node = page_list_append(l, page_list_rows_per_page(l->cols, l->rows));
    if (!node) return nullptr;
    page_list_trim(l);
    return node;
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

    /* One page is enough to start: it is sized to hold at least the screen,
     * and the rest arrives as the screen scrolls. */
    return page_list_append(l, page_list_rows_per_page(cols, rows)) != nullptr;
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
        const size_t n = (size_t)node->page.capacity.rows;
        if (row_index < n) return Pin(node, (CellCountInt)row_index, 0);
        row_index -= n;
        node = node->next;
    }
    return Pin();
}

/* The first row the screen shows: the last `rows` rows of the list. */
inline Pin page_list_screen_start(const PageList *l) {
    if (l->row_count < (size_t)l->rows) return page_list_pin(l, 0);
    return page_list_pin(l, l->row_count - (size_t)l->rows);
}

/* A pin for a screen coordinate, where y is counted from the top of the
 * screen rather than from the start of the scrollback. */
inline Pin page_list_screen_pin(const PageList *l, CellCountInt x,
                                CellCountInt y) {
    Pin p = page_list_screen_start(l);
    if (!p.valid()) return p;
    if (y > 0 && !p.down((size_t)y)) return Pin();
    p.x = x;
    return p;
}

} /* namespace terminal */
} /* namespace wisp */

#endif /* WISP_TERMINAL_PAGE_LIST_HPP */
