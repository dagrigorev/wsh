/* Transliterated from the test blocks in Ghostty src/terminal/PageList.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Upstream's tests: same inputs, same assertions, same names. Built with
 * WISP_IS_TEST and WISP_SLOW_RUNTIME_SAFETY, as upstream's tests run.
 *
 * Mapping:
 *   try init(alloc, .{...})        ASSERT_TRUE(PageList::init(alloc, opts, &s))
 *   defer s.deinit()               ListHolder (deinit on scope exit)
 *   s.pin(pt).?                    s.pin(pt).value
 *   expectError(E, f)              ASSERT_TRUE(f == ...::E)
 */

#include <string.h>

#include "test_helpers.h"
#include "../vt/page_list.hpp"

using namespace wisp;
using namespace wisp::vt;

typedef PageList::Pin Pin;
typedef PageList::Node Node;
typedef PageList::Viewport Viewport;
typedef PageList::Options Options;
typedef PageList::Builder Builder;
typedef PageList::PageAllocation PageAllocation;
typedef page::Page Page;
typedef page::Capacity Capacity;
typedef point::Point Point;

static zigstd::Allocator talloc() { return zigstd::testing_allocator(); }

struct ListHolder {
    PageList s;
    bool ok;
    ListHolder(zigstd::Allocator a, const Options &o) { ok = PageList::init(a, o, &s); }
    explicit ListHolder(const Options &o) { ok = PageList::init(talloc(), o, &s); }
    ~ListHolder() {
        if (ok) s.deinit();
    }
    PageList *operator->() { return &s; }
    PageList &operator*() { return s; }
};

static Options opts(size::CellCountInt cols, size::CellCountInt rows, Maybe<size_t> max_size = Maybe<size_t>(),
                    Maybe<size_t> max_lines = Maybe<size_t>()) {
    return Options(cols, rows, max_size, max_lines);
}

static uint32_t cpAt(Page *p, size_t x, size_t y) { return p->getRowAndCell(x, y).cell->codepoint(); }

/* ---- Builder / PageAllocation ---- */

TEST(page_list, PageList_Builder_transfers_mixed_width_pages) {
    /* Build two populated pages whose widths differ from each other and from
     * the final active-area width. The first page also includes one row of
     * incidental history above the three-row active area. */
    PageList result;
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(4, 3), &builder));

        Page *first = builder.allocatePage(Capacity(2, 2));
        first->size.rows = 2;
        *first->getRowAndCell(0, 0).cell = page::Cell::init('A');

        Page *second = builder.allocatePage(Capacity(4, 2));
        second->size.rows = 2;
        *second->getRowAndCell(0, 0).cell = page::Cell::init('B');

        ASSERT_TRUE(builder.finish(&result) == Builder::FinishError::none);
        builder.deinit();
    }

    /* Successful finish transfers ownership and initializes the PageList's
     * desired geometry, viewport, and required tracked viewport pin. */
    ASSERT_TRUE(4 == result.cols);
    ASSERT_TRUE(3 == result.rows);
    ASSERT_TRUE(2 == result.totalPages());
    ASSERT_TRUE(1 == result.countTrackedPins());
    ASSERT_TRUE(Viewport::active == result.viewport);

    /* Complete pages and their contents are preserved in insertion order,
     * including widths which have not yet been reflowed. */
    const Pin screen_top = result.getTopLeft(point::Tag::screen);
    ASSERT_TRUE(2 == screen_top.node->cols());
    ASSERT_TRUE('A' == cpAt(screen_top.node->page(), 0, 0));

    /* The active area is calculated backward from the newest page, so it
     * begins at row one of the oldest page and leaves row zero as history. */
    const Pin active_top = result.getTopLeft(point::Tag::active);
    ASSERT_TRUE(screen_top.node == active_top.node);
    ASSERT_TRUE(1 == active_top.y);
    ASSERT_TRUE(4 == active_top.node->next->cols());
    ASSERT_TRUE('B' == cpAt(active_top.node->next->page(), 0, 0));

    result.assertIntegrity();
    result.deinit();
}

TEST(page_list, PageList_Builder_validates_the_finished_list) {
    PageList out;
    /* The desired PageList geometry must describe a non-empty screen. */
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(0, 1), &builder));
        ASSERT_TRUE(builder.finish(&out) == Builder::FinishError::InvalidDimensions);
        builder.deinit();
    }

    /* A PageList cannot be finished without any backing pages. */
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(1, 1), &builder));
        ASSERT_TRUE(builder.finish(&out) == Builder::FinishError::NoPages);
        builder.deinit();
    }

    /* Allocated capacity alone is insufficient: callers must populate a
     * nonzero logical page size before transferring ownership. */
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(1, 1), &builder));
        Page *page = builder.allocatePage(Capacity(1, 1));
        page->size.rows = 0;
        ASSERT_TRUE(builder.finish(&out) == Builder::FinishError::InvalidPageDimensions);
        builder.deinit();
    }

    /* The populated pages must contain enough rows to cover the active area. */
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(1, 2), &builder));
        Page *page = builder.allocatePage(Capacity(1, 1));
        page->size.rows = 1;
        ASSERT_TRUE(builder.finish(&out) == Builder::FinishError::InsufficientRows);
        builder.deinit();
    }
}

TEST(page_list, PageList_Builder_finish_is_transactional_on_allocation_failure) {
    /* Construct a valid builder so finish reaches its fallible bookkeeping
     * allocations after all page and geometry validation succeeds. */
    zigstd::FailingAllocator failing(talloc(), SIZE_MAX);
    Builder builder;
    ASSERT_TRUE(Builder::init(failing.allocator(), opts(1, 1), &builder));
    Page *page = builder.allocatePage(Capacity(1, 1));
    page->size.rows = 1;

    /* The pools are preheated, so the next general allocation is the tracked
     * viewport pin map created by finish. */
    failing.fail_index = failing.alloc_index;
    PageList out;
    ASSERT_TRUE(builder.finish(&out) == Builder::FinishError::OutOfMemory);
    ASSERT_TRUE(failing.has_induced_failure);

    /* Failed finish leaves page ownership with the builder so its normal
     * deinit path can release the still-linked page. */
    ASSERT_TRUE(builder.pages.first != nullptr);
    ASSERT_TRUE(builder.pages.first == builder.pages.last);
    builder.deinit();
}

TEST(page_list, PageList_PageAllocation_finalizes_pages_and_preserves_live_state) {
    /* Build an existing list with history, a two-row active area, an external
     * active pin, and a pinned viewport whose absolute offset is cached. */
    PageList result;
    {
        Builder builder;
        ASSERT_TRUE(Builder::init(talloc(), opts(4, 2), &builder));

        Page *first = builder.allocatePage(Capacity(3, 2));
        first->size.rows = 2;
        *first->getRowAndCell(0, 0).cell = page::Cell::init('C');

        Page *second = builder.allocatePage(Capacity(4, 2));
        second->size.rows = 2;
        *second->getRowAndCell(0, 0).cell = page::Cell::init('D');

        ASSERT_TRUE(builder.finish(&result) == Builder::FinishError::none);
        builder.deinit();
    }

    Node *old_first = result.pages.first;
    Node *old_last = result.pages.last;
    const Pin active_top = result.getTopLeft(point::Tag::active);
    Pin *tracked_active = result.trackPin(active_top);
    result.scroll(PageList::Scroll::rowAt(1));
    ASSERT_TRUE(Viewport::pin == result.viewport);
    ASSERT_TRUE(1 == result.scrollbar().offset);

    /* Prepend differently sized historical pages newest-first. Each page is
     * populated while detached and joins the live list only on success. */
    {
        PageAllocation allocation;
        ASSERT_TRUE(result.allocatePage(Capacity(4, 1), &allocation));
        Page *page = allocation.page();
        page->size.rows = 1;
        *page->getRowAndCell(0, 0).cell = page::Cell::init('B');
        ASSERT_TRUE(allocation.finalize(PageAllocation::Location::prepend) == PageAllocation::FinalizeError::none);
        allocation.deinit();
    }
    {
        PageAllocation allocation;
        ASSERT_TRUE(result.allocatePage(Capacity(2, 2), &allocation));
        Page *page = allocation.page();
        page->size.rows = 2;
        *page->getRowAndCell(0, 0).cell = page::Cell::init('A');
        ASSERT_TRUE(allocation.finalize(PageAllocation::Location::prepend) == PageAllocation::FinalizeError::none);
        allocation.deinit();
    }

    /* Repeated prepends reconstruct oldest-to-newest order without replacing
     * any existing nodes or tracked pins. */
    ASSERT_TRUE(4 == result.totalPages());
    ASSERT_TRUE(7 == result.total_rows);
    ASSERT_TRUE(old_last == result.pages.last);
    ASSERT_TRUE(old_first == result.pages.first->next->next);
    ASSERT_TRUE('A' == cpAt(result.pages.first->page(), 0, 0));
    ASSERT_TRUE('B' == cpAt(result.pages.first->next->page(), 0, 0));
    ASSERT_TRUE(active_top.eql(result.getTopLeft(point::Tag::active)));
    ASSERT_TRUE(active_top.eql(*tracked_active));

    /* The viewport remains pinned to the same content, while its cached row
     * offset and the scrollbar total include the three new historical rows. */
    ASSERT_TRUE(Viewport::pin == result.viewport);
    ASSERT_TRUE(old_first == result.viewport_pin->node);
    ASSERT_TRUE(1 == result.viewport_pin->y);
    const PageList::Scrollbar scrollbar_state = result.scrollbar();
    ASSERT_TRUE(7 == scrollbar_state.total);
    ASSERT_TRUE(4 == scrollbar_state.offset);
    ASSERT_TRUE(2 == scrollbar_state.len);

    result.assertIntegrity();
    result.deinit();
}

TEST(page_list, PageList_PageAllocation_stays_detached_until_finalize) {
    ListHolder result(opts(1, 1));

    Node *initial_first = result->pages.first;
    Node *initial_last = result->pages.last;
    const size_t initial_total_rows = result->total_rows;
    const size_t initial_page_size = result->page_size;

    /* Allocating and populating a detached page does not alter any live list
     * links or accounting. Deinit returns it to the same PageList pools. */
    PageAllocation detached;
    ASSERT_TRUE(result->allocatePage(Capacity(1, 1), &detached));
    detached.page()->size.rows = 1;
    ASSERT_TRUE(initial_first == result->pages.first);
    ASSERT_TRUE(initial_last == result->pages.last);
    ASSERT_TRUE(initial_total_rows == result->total_rows);
    ASSERT_TRUE(initial_page_size == result->page_size);
    result->assertIntegrity();
    detached.deinit();
    result->assertIntegrity();

    /* Invalid populated dimensions leave ownership with the allocation so it
     * can still be released normally. */
    PageAllocation invalid;
    ASSERT_TRUE(result->allocatePage(Capacity(1, 1), &invalid));
    ASSERT_TRUE(invalid.finalize(PageAllocation::Location::prepend) ==
                PageAllocation::FinalizeError::InvalidPageDimensions);

    ASSERT_TRUE(initial_first == result->pages.first);
    ASSERT_TRUE(initial_last == result->pages.last);
    ASSERT_TRUE(initial_total_rows == result->total_rows);
    ASSERT_TRUE(initial_page_size == result->page_size);
    result->assertIntegrity();
    invalid.deinit();
}

TEST(page_list, PageList_PageAllocation_rejects_limits_before_modifying_the_destination) {
    ListHolder result(opts(1, 1, (size_t)0));

    /* The effective minimum permits one complete page beyond the active page.
     * Fill that allowance so the following allocation exceeds the byte limit. */
    {
        PageAllocation allocation;
        ASSERT_TRUE(result->allocatePage(Capacity(1, 1), &allocation));
        allocation.page()->size.rows = 1;
        ASSERT_TRUE(allocation.finalize(PageAllocation::Location::prepend) == PageAllocation::FinalizeError::none);
        allocation.deinit();
    }

    Node *before_first = result->pages.first;
    const size_t before_total_rows = result->total_rows;
    const size_t before_page_size = result->page_size;

    PageAllocation allocation;
    ASSERT_TRUE(result->allocatePage(Capacity(1, 1), &allocation));
    allocation.page()->size.rows = 1;
    ASSERT_TRUE(allocation.finalize(PageAllocation::Location::prepend) ==
                PageAllocation::FinalizeError::MaxSizeExceeded);

    ASSERT_TRUE(before_first == result->pages.first);
    ASSERT_TRUE(before_total_rows == result->total_rows);
    ASSERT_TRUE(before_page_size == result->page_size);
    result->assertIntegrity();
    allocation.deinit();
}

TEST(page_list, PageList_PageAllocation_allocation_failure_leaves_list_unchanged) {
    zigstd::FailingAllocator failing(talloc(), SIZE_MAX);
    ListHolder result(failing.allocator(), opts(1, 1));

    Node *initial_first = result->pages.first;
    const size_t initial_total_rows = result->total_rows;
    const size_t initial_page_size = result->page_size;

    /* Existing pool capacity is deliberately an implementation detail. Allow
     * preheated slots to succeed until allocation reaches node-pool growth. */
    failing.fail_index = failing.alloc_index;
    PageAllocation allocations[64];
    size_t allocation_count = 0;

    bool failed = false;
    for (size_t i = 0; i < 64; i++) {
        if (!result->allocatePage(Capacity(1, 1), &allocations[allocation_count])) {
            failed = true;
            break;
        }
        allocation_count += 1;
    }
    ASSERT_TRUE(failed);
    ASSERT_TRUE(failing.has_induced_failure);

    /* Detached allocations and failed pool growth never publish into the live
     * list; the deferred cleanup returns every successful allocation. */
    ASSERT_TRUE(initial_first == result->pages.first);
    ASSERT_TRUE(initial_total_rows == result->total_rows);
    ASSERT_TRUE(initial_page_size == result->page_size);
    result->assertIntegrity();
    for (size_t i = 0; i < allocation_count; i++) allocations[i].deinit();
}

/* @@TESTS@@ */

/* Wisp: std.testing.allocator's leak check. Runs last (registration order). */
TEST(page_list, zz_Wisp_no_leaks) { ASSERT_TRUE(zigstd::testing_state().live == 0); }
