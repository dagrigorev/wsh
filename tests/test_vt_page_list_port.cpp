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
#include "../zigstd/random.hpp"

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

/* ---- test helpers (upstream's *ForTest functions) ---- */

typedef PageList::IncrementalCompressionState CState;
typedef PageList::IncrementalCompressionResult CResult;
typedef PageList::CompressMode CMode;

static bool mixedWidthPinListForTest(PageList *result) {
    if (!PageList::init(talloc(), opts(2, 1), result)) return false;

    /* This deliberately constructs a layout that normal PageList operations
     * do not expose yet. Keep integrity checks paused through deinit so the
     * fixture can exercise mixed-width traversal in isolation. */
    result->pauseIntegrityChecks(true);

    const size::CellCountInt widths[] = {4, 3};
    for (size::CellCountInt c : widths) {
        Node *node = result->createPage(PageList::CreatePage(Capacity(c, 1)));
        if (!node) return false;
        node->page()->size.rows = 1;
        result->pages.append(node);
        result->total_rows += 1;
    }

    /* Desired geometry is wider than the first and last stored pages. */
    result->cols = 4;
    return true;
}

/* Grow a test PageList until it contains at least `count` complete history
 * pages. The production cold-page boundary is intentionally reused here so
 * tests do not duplicate the row-to-page arithmetic. */
static bool growColdPagesForTest(PageList *self, size_t count) {
    for (;;) {
        Node *active_node = self->getTopLeft(point::Tag::active).node;
        size_t cold_count = 0;
        for (Node *node = self->pages.first; node; node = node->next) {
            if (node == active_node) break;
            cold_count += 1;
        }

        if (cold_count >= count) return true;
        Node *g;
        if (!self->grow(&g)) return false;
    }
}

/* Fill the current tail page to capacity without allocating a successor.
 * Capturing the tail before the loop makes this stop at the allocation
 * boundary needed by bounded-pruning tests. */
static bool fillLastPageForTest(PageList *self) {
    Node *last = self->pages.last;
    while (last->rows() < last->capacity().rows) {
        Node *g;
        if (!self->grow(&g)) return false;
    }
    return true;
}

/* Verify every live page belongs to the current validity epoch, has an
 * allocated generation below the next serial, and validates through the same
 * pointer-plus-generation lookup used by external references. */
static bool expectLivePageSerialsValidForTest(const PageList *self) {
    for (const Node *live = self->pages.first; live; live = live->next) {
        if (!(live->serial >= self->page_serial_epoch)) return false;
        if (!(live->serial < self->page_serial)) return false;
        if (!self->nodeIsValid(live, live->serial)) return false;
    }
    return true;
}

static Node *growNode(PageList *s) {
    Node *g = nullptr;
    const bool ok = s->grow(&g);
    assert(ok);
    (void)ok;
    return g;
}

static bool stateEq(const CState &a, const CState &b) {
    return a.flags.did_compress == b.flags.did_compress && a.flags.verifying == b.flags.verifying &&
           a.activity_serial == b.activity_serial && a.last_serial.has == b.last_serial.has &&
           (!a.last_serial.has || a.last_serial.value == b.last_serial.value) && a.next_serial == b.next_serial;
}

static CState mkState(bool did, bool ver, uint64_t act, Maybe<uint64_t> last, uint64_t next) {
    CState s;
    s.flags.did_compress = did;
    s.flags.verifying = ver;
    s.activity_serial = act;
    s.last_serial = last;
    s.next_serial = next;
    return s;
}

static bool pointEq(const Maybe<Point> &a, const Point &b) { return a.has && a.value.eql(b); }

/* ---- Pin movement ---- */

TEST(page_list, PageList_Pin_row_movement_clamps_across_mixed_width_pages) {
    PageList s;
    ASSERT_TRUE(mixedWidthPinListForTest(&s));

    Node *first = s.pages.first;
    Node *second = first->next;
    Node *third = second->next;

    ASSERT_TRUE(Pin::at(third, 2, 0).eql(Pin::at(second, 3, 0).down(1).value));
    ASSERT_TRUE(Pin::at(first, 1, 0).eql(Pin::at(second, 3, 0).up(1).value));

    {
        const Pin::Overflow o = Pin::at(second, 3, 0).downOverflow(10);
        ASSERT_TRUE(o.tag == Pin::Overflow::Tag::overflow);
        ASSERT_TRUE(Pin::at(third, 2, 0).eql(o.overflow.end));
    }
    {
        const Pin::Overflow o = Pin::at(second, 3, 0).upOverflow(10);
        ASSERT_TRUE(o.tag == Pin::Overflow::Tag::overflow);
        ASSERT_TRUE(Pin::at(first, 1, 0).eql(o.overflow.end));
    }
    s.deinit();
}

TEST(page_list, PageList_Pin_wrapping_crosses_mixed_width_pages) {
    PageList s;
    ASSERT_TRUE(mixedWidthPinListForTest(&s));

    Node *first = s.pages.first;
    Node *second = first->next;
    Node *third = second->next;

    ASSERT_TRUE(Pin::at(third, 2, 0).eql(Pin::at(first, 1, 0).rightWrap(7).value));
    ASSERT_TRUE(Pin::at(second, 0, 0).eql(Pin::at(third, 2, 0).leftWrap(6).value));
    ASSERT_FALSE(Pin(first).leftWrap(1).has);
    ASSERT_FALSE(Pin::at(third, 2, 0).rightWrap(1).has);
    s.deinit();
}

TEST(page_list, PageList_Pin_rejects_columns_beyond_mixed_width_page_bounds) {
    PageList s;
    ASSERT_TRUE(mixedWidthPinListForTest(&s));

    ASSERT_TRUE(s.pin(Point::screen(1, 0)).has);
    ASSERT_FALSE(s.pin(Point::screen(2, 0)).has);
    ASSERT_TRUE(s.pin(Point::screen(3, 1)).has);
    ASSERT_FALSE(s.pin(Point::screen(3, 2)).has);
    s.deinit();
}

TEST(page_list, PageList_Pin_rightWrap_exact_row_multiple) {
    ListHolder s(opts(10, 3));

    const Pin start = s->pin(Point::active(5, 0)).value;
    const Pin wrapped = start.rightWrap(14).value;
    (void)wrapped.rowAndCell();

    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, wrapped), Point::active(9, 1)));
}

TEST(page_list, PageList_Pin_leftWrap_exact_row_multiple) {
    ListHolder s(opts(10, 3));

    const Pin start = s->pin(Point::active(5, 2)).value;
    const Pin wrapped = start.leftWrap(15).value;
    (void)wrapped.rowAndCell();

    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, wrapped), Point::active(0, 1)));
}

TEST(page_list, PageList_Pin_rightWrap_maximum_distance) {
    ListHolder s(opts(1, 3));

    const Pin start = s->pin(Point::active(0, 0)).value;
    ASSERT_FALSE(start.rightWrap(SIZE_MAX).has);
}

TEST(page_list, PageList_Pin_leftWrap_maximum_distance) {
    ListHolder s(opts(1, 3));

    const Pin start = s->pin(Point::active(0, 2)).value;
    ASSERT_FALSE(start.leftWrap(SIZE_MAX).has);
}

/* ---- Compression ---- */

TEST(page_list, PageList_incremental_compression_skips_visible_history) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 3));

    const uint64_t initial_activity = s->page_compression.activity_serial;
    ASSERT_TRUE(initial_activity > 0);

    s->scroll(PageList::Scroll::top());
    const uint64_t top_activity = s->page_compression.activity_serial;
    ASSERT_TRUE(top_activity != initial_activity);
    ASSERT_TRUE(CResult::complete == s->compress(CMode::drain));

    Node *first = s->pages.first;
    Node *second = first->next;
    ASSERT_TRUE(first == s->getTopLeft(point::Tag::viewport).node);
    ASSERT_TRUE(first == s->getBottomRight(point::Tag::viewport).value.node);
    ASSERT_FALSE(first->isCompressed());

    PageList::CompressionIterator eligible = PageList::CompressionIterator::init(&*s);
    size_t compressed_pages = 0;
    while (Node *node = eligible.next()) {
        compressed_pages += 1;
        ASSERT_TRUE(node->isCompressed());
    }
    ASSERT_TRUE(compressed_pages > 0);

    /* Move the viewport to the start of the second page. Rendering it restores
     * that page, while the first page which just left view becomes eligible. */
    s->scroll(PageList::Scroll::rowAt(first->rows()));
    ASSERT_TRUE(second == s->getTopLeft(point::Tag::viewport).node);
    (void)second->page();
    ASSERT_FALSE(second->isCompressed());
    (void)s->compress(CMode::drain);
    ASSERT_TRUE(first->isCompressed());
    ASSERT_FALSE(second->isCompressed());

    /* Returning to the active area makes every complete historical page
     * eligible again, including the page which was just visible. */
    s->scroll(PageList::Scroll::active());
    (void)s->compress(CMode::drain);
    ASSERT_TRUE(second->isCompressed());
    ASSERT_FALSE(s->page_compression.flags.did_compress);
    ASSERT_FALSE(s->page_compression.flags.verifying);
    ASSERT_FALSE(s->page_compression.last_serial.has);
    ASSERT_TRUE(0 == s->page_compression.next_serial);
}

TEST(page_list, PageList_owns_incremental_compression_state) {
    ListHolder s(opts(80, 24));

    const CState state = mkState(true, true, 42, (uint64_t)42, 43);
    const CState only43 = mkState(false, false, 43, Maybe<uint64_t>(), 0);
    const CState only42 = mkState(false, false, 42, Maybe<uint64_t>(), 0);

    s->page_compression = state;
    s->scroll(PageList::Scroll::top());
    ASSERT_TRUE(stateEq(only43, s->page_compression));

    /* Every scroll restarts traversal, even if clamping leaves the viewport in
     * the same place. Missing an eligible page is worse than a no-op pass. */
    s->page_compression = state;
    s->scroll(PageList::Scroll::top());
    ASSERT_TRUE(stateEq(only43, s->page_compression));

    s->page_compression = state;
    s->scroll(PageList::Scroll::active());
    ASSERT_TRUE(stateEq(only43, s->page_compression));

    s->page_compression = state;
    {
        PageList::Resize r;
        r.cols = (size::CellCountInt)80;
        r.rows = (size::CellCountInt)24;
        ASSERT_TRUE(s->resize(r));
    }
    ASSERT_TRUE(stateEq(only43, s->page_compression));

    s->page_compression = state;
    s->reset();
    ASSERT_TRUE(stateEq(only42, s->page_compression));

    s->page_compression = state;
    ASSERT_TRUE(CResult::complete == s->compress(CMode::full));
    ASSERT_TRUE(stateEq(only42, s->page_compression));

    s->page_compression = state;
    PageList cloned;
    ASSERT_TRUE(s->clone(talloc(), PageList::Clone(Point::active()), &cloned) == page::PageError::none);
    ASSERT_TRUE(stateEq(CState(), cloned.page_compression));
    cloned.deinit();
}

TEST(page_list, PageList_replacements_preserve_compression_continuation_and_mark_activity) {
    ListHolder s(opts(80, 24));

    const CState state = mkState(false, true, 42, (uint64_t)7, 8);
    const CState expected = mkState(false, true, 43, (uint64_t)7, 8);

    s->page_compression = state;
    Node *replacement;
    ASSERT_TRUE(s->increaseCapacity(s->pages.first, Maybe<PageList::IncreaseCapacity>(), &replacement) ==
                PageList::IncreaseCapacityError::none);
    ASSERT_TRUE(stateEq(expected, s->page_compression));

    s->page_compression = state;
    Node *compacted;
    ASSERT_TRUE(s->compact(replacement, &compacted));
    ASSERT_TRUE(compacted != nullptr);
    ASSERT_TRUE(stateEq(expected, s->page_compression));
}

TEST(page_list, PageList_incremental_compression_bounds_inspected_pages) {
    const size_t max_inspected = PageList::incremental_compression_max_inspected;
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, max_inspected + 1));

    /* Precompress every candidate so the incremental pass exercises its
     * metadata-only skip budget without stopping at a resident attempt. */
    (void)s->compress(CMode::full);
    s->page_compression.reset();
    s->page_compression.markActivity();
    ASSERT_TRUE(max_inspected + 1 == s->memoryStats().compressed_pages);

    Node *expected_last = s->pages.first;
    for (size_t i = 1; i < max_inspected; i++) expected_last = expected_last->next;

    const CResult first = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == first);
    ASSERT_TRUE(expected_last->serial == s->page_compression.last_serial.value);

    const CResult second = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == second);
    ASSERT_TRUE(s->page_compression.flags.verifying);
    ASSERT_FALSE(s->page_compression.last_serial.has);

    /* The verification pass is bounded independently, too. */
    ASSERT_TRUE(CResult::pending == s->compress(CMode::incremental));
    ASSERT_TRUE(CResult::complete == s->compress(CMode::incremental));
}

TEST(page_list, PageList_incremental_compression_advances_after_failure) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 2));

    Node *first = s->pages.first;
    Node *second = first->next;
    zigstd::DefaultPrng prng = zigstd::DefaultPrng::init(0x494E435250415353ull);
    zigstd::Random(&prng).bytes(first->page()->memory, first->page()->memory_len);

    const CResult failed = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == failed);
    ASSERT_FALSE(first->isCompressed());

    /* The unsuccessful first page does not stall the pass. The next step
     * continues at the following serial and compresses that page. */
    const CResult continued = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == continued);
    ASSERT_TRUE(second->isCompressed());

    /* Wisp: the random bytes are not a valid page; zero them so deinit's
     * integrity check sees a well-formed (empty) page again. */
    first->page()->reinit();
    first->page()->size.rows = first->capacity().rows;
}

TEST(page_list, PageList_incremental_compression_advances_after_allocation_failure) {
    zigstd::FailingAllocator failing(talloc(), SIZE_MAX);
    const zigstd::Allocator alloc = failing.allocator();
    ListHolder s(alloc, opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 2));
    Node *first = s->pages.first;
    Node *second = first->next;

    /* Pool preheating supplies compression scratch. Failing the allocator's
     * next request therefore rejects the exact encoded allocation while the
     * source page and pass remain valid. */
    failing.fail_index = failing.alloc_index;
    const CResult failed = s->compress(CMode::incremental);
    ASSERT_TRUE(failing.has_induced_failure);
    ASSERT_TRUE(CResult::pending == failed);
    ASSERT_FALSE(first->isCompressed());

    /* Allow allocations again. The pass must continue with the following page
     * rather than retrying the failed candidate. */
    failing.fail_index = SIZE_MAX;
    const CResult continued = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == continued);
    ASSERT_TRUE(second->isCompressed());
}

TEST(page_list, PageList_incremental_compression_advances_after_decommit_failure) {
    typedef PageList::compressPage_tw tw;

    {
        ListHolder s(opts(80, 24));
        ASSERT_TRUE(growColdPagesForTest(&*s, 2));

        tw::errorAlways(PageList::CompressPageTw::decommit, PageList::CompressPageTwError::DecommitFailed);
        const CResult failed = s->compress(CMode::incremental);
        ASSERT_TRUE(CResult::pending == failed);
        ASSERT_FALSE(s->pages.first->isCompressed());
        ASSERT_TRUE(tw::end(tripwire::ResetMode::reset));

        /* The failed candidate remains resident and the pass continues at the
         * next serial once reclamation is available again. */
        const CResult continued = s->compress(CMode::incremental);
        ASSERT_TRUE(CResult::pending == continued);
        ASSERT_TRUE(s->pages.first->next->isCompressed());
    }
    (void)tw::end(tripwire::ResetMode::reset);
}

TEST(page_list, PageList_incremental_compression_restarts_after_replacement) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));

    const CResult initial = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == initial);
    ASSERT_TRUE(s->pages.first->isCompressed());

    Node *old = s->pages.first;
    const uint64_t old_serial = old->serial;
    Node *replacement = old;
    while (replacement->page()->memory_len <= PageList::std_size) {
        ASSERT_TRUE(s->increaseCapacity(replacement, PageList::IncreaseCapacity::grapheme_bytes, &replacement) ==
                    PageList::IncreaseCapacityError::none);
    }
    ASSERT_TRUE(replacement->serial != old_serial);
    ASSERT_TRUE(replacement->page()->memory_len > PageList::std_size);
    ASSERT_FALSE(replacement->isCompressed());

    /* The exact continuation serial disappeared with the old node. The pass
     * restarts at the first page and considers the oversized replacement. */
    const CResult restarted = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == restarted);
    ASSERT_TRUE(replacement->isCompressed());
}

TEST(page_list, PageList_incremental_compression_restarts_after_reset) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));

    const CResult initial = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == initial);
    ASSERT_TRUE(s->pages.first->isCompressed());

    /* Reset replaces every page and clears the PageList-owned traversal. */
    s->reset();
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));
    const CResult restarted = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == restarted);
    ASSERT_TRUE(s->pages.first->isCompressed());
}

TEST(page_list, PageList_incremental_compression_restarts_after_active_boundary_resize) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));

    const CResult initial = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == initial);
    ASSERT_TRUE(s->pages.first->isCompressed());

    Node *first = s->pages.first;
    const size::CellCountInt all_rows = (size::CellCountInt)s->total_rows;
    {
        PageList::Resize r;
        r.rows = all_rows;
        ASSERT_TRUE(s->resize(r));
    }
    ASSERT_TRUE(first == s->getTopLeft(point::Tag::active).node);

    /* Restore the page while it is active. Resize reset the traversal, and
     * active contents remain ineligible. */
    (void)first->page();
    const CResult active = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == active);
    ASSERT_TRUE(CResult::complete == s->compress(CMode::incremental));

    /* Shrinking the active area makes the page fully historical again. The
     * resize reset the PageList-owned cursor, so the next step can reclaim it. */
    {
        PageList::Resize r;
        r.rows = (size::CellCountInt)24;
        ASSERT_TRUE(s->resize(r));
    }
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));
    ASSERT_TRUE(CResult::pending == s->compress(CMode::incremental));
    ASSERT_TRUE(first->isCompressed());
}

TEST(page_list, PageList_incremental_compression_restarts_after_prune_reuse) {
    ListHolder s(opts(80, 24, 2 * PageList::PagePool::item_size));
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));

    const CResult initial = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == initial);
    ASSERT_TRUE(s->pages.first->isCompressed());

    Node *reused = s->pages.first;
    const uint64_t old_serial = reused->serial;
    while (s->pages.last->rows() < s->pages.last->capacity().rows) {
        (void)growNode(&*s);
    }
    ASSERT_TRUE(reused == growNode(&*s));
    ASSERT_TRUE(reused->serial != old_serial);

    /* Make the remaining old page fully historical. The continuation serial
     * disappeared when its node was recycled, so the pass safely restarts. */
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));
    (void)s->compress(CMode::incremental);
    ASSERT_TRUE(1 == s->memoryStats().compressed_pages);
}

TEST(page_list, PageList_bounded_pruning_after_partial_erase_preserves_live_serials) {
    ListHolder s(opts(80, 24, 2 * PageList::PagePool::item_size));

    while (s->totalPages() < 2) (void)growNode(&*s);
    Node *first = s->pages.first;
    const uint64_t old_serial = first->serial;
    const size_t old_rows = first->rows();

    s->eraseHistory(Point::history(0, 0));
    ASSERT_TRUE(first == s->pages.first);
    ASSERT_TRUE(old_rows - 1 == first->rows());
    ASSERT_FALSE(s->nodeIsValid(first, old_serial));

    ASSERT_TRUE(fillLastPageForTest(&*s));
    (void)growNode(&*s);
    ASSERT_TRUE(expectLivePageSerialsValidForTest(&*s));
}

TEST(page_list, PageList_partial_erase_restarts_compression_before_continuation) {
    const size_t max_inspected = PageList::incremental_compression_max_inspected;
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, max_inspected + 1));
    (void)s->compress(CMode::full);

    Node *first = s->pages.first;
    ASSERT_TRUE(first->isCompressed());

    Node *marker = first;
    for (size_t i = 1; i < max_inspected; i++) marker = marker->next;
    s->page_compression = mkState(false, true, 0, marker->serial, s->page_serial);

    const uint64_t activity = s->page_compression.activity_serial;
    s->eraseHistory(Point::history(0, 0));
    ASSERT_FALSE(first->isCompressed());
    ASSERT_TRUE(activity != s->page_compression.activity_serial);

    /* The changed generation is before the saved marker, so continuation must
     * restart and recompress it instead of reporting verification complete. */
    ASSERT_TRUE(CResult::pending == s->compress(CMode::incremental));
    ASSERT_TRUE(first->isCompressed());
}

TEST(page_list, PageList_bounded_pruning_after_split_invalidation_preserves_live_serials) {
    ListHolder s(opts(80, 24, 2 * PageList::PagePool::item_size));

    while (s->totalPages() < 2) (void)growNode(&*s);
    Node *first = s->pages.first;
    const uint64_t old_serial = first->serial;
    const uint64_t activity = s->page_compression.activity_serial;

    ASSERT_TRUE(s->split(Pin(first, (size::CellCountInt)(first->rows() / 2), 0)) == PageList::SplitError::none);
    ASSERT_FALSE(s->nodeIsValid(first, old_serial));
    ASSERT_TRUE(activity != s->page_compression.activity_serial);

    ASSERT_TRUE(fillLastPageForTest(&*s));
    (void)growNode(&*s);
    ASSERT_TRUE(expectLivePageSerialsValidForTest(&*s));
}

TEST(page_list, PageList_repeated_bounded_pruning_after_split_preserves_live_serials) {
    ListHolder s(opts(80, 24, 3 * PageList::PagePool::item_size));

    const uint64_t epoch = s->page_serial_epoch;
    while (s->totalPages() < 3) (void)growNode(&*s);
    Node *first = s->pages.first;
    ASSERT_TRUE(s->split(Pin(first, (size::CellCountInt)(first->rows() / 2), 0)) == PageList::SplitError::none);

    /* The split target has a fresh serial but precedes older successor pages.
     * Prune both the old source and then that target while verifying ordinary
     * list mutation does not advance the whole-list validity epoch. */
    for (int k = 0; k < 2; k++) {
        while (s->pages.last->rows() < s->pages.last->capacity().rows) {
            (void)growNode(&*s);
        }
        (void)growNode(&*s);

        /* Ordinary pruning invalidates one generation at a time through live
         * list validation; only reset may begin a new whole-list epoch. */
        ASSERT_TRUE(epoch == s->page_serial_epoch);
        ASSERT_TRUE(expectLivePageSerialsValidForTest(&*s));
    }
}

TEST(page_list, PageList_bounded_pruning_after_front_replacement_preserves_live_serials) {
    ListHolder s(opts(80, 24, 2 * PageList::PagePool::item_size));

    while (s->totalPages() < 2) (void)growNode(&*s);
    Node *old = s->pages.first;
    const uint64_t old_serial = old->serial;
    Node *replacement;
    ASSERT_TRUE(s->increaseCapacity(old, Maybe<PageList::IncreaseCapacity>(), &replacement) ==
                PageList::IncreaseCapacityError::none);
    ASSERT_TRUE(replacement != old);
    ASSERT_FALSE(s->nodeIsValid(old, old_serial));

    ASSERT_TRUE(fillLastPageForTest(&*s));
    (void)growNode(&*s);

    ASSERT_TRUE(expectLivePageSerialsValidForTest(&*s));
}

static bool statsEq(const PageList::MemoryStats &a, const PageList::MemoryStats &b) {
    return a.resident_pages == b.resident_pages && a.compressed_pages == b.compressed_pages &&
           a.raw_bytes == b.raw_bytes && a.resident_raw_bytes == b.resident_raw_bytes &&
           a.decommitted_raw_bytes == b.decommitted_raw_bytes &&
           a.resident_backing_bytes == b.resident_backing_bytes && a.encoded_bytes == b.encoded_bytes;
}

static uint8_t *dupeMem(const Page *p) {
    uint8_t *d = (uint8_t *)malloc(p->memory_len);
    memcpy(d, p->memory, p->memory_len);
    return d;
}

static bool allZero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

TEST(page_list, PageList_bounded_pruning_after_middle_replacement_preserves_live_serials) {
    ListHolder s(opts(80, 24, 3 * PageList::PagePool::item_size));

    while (s->totalPages() < 3) (void)growNode(&*s);
    Node *old = s->pages.first->next;
    const uint64_t old_serial = old->serial;
    Node *replacement;
    ASSERT_TRUE(s->increaseCapacity(old, Maybe<PageList::IncreaseCapacity>(), &replacement) ==
                PageList::IncreaseCapacityError::none);
    ASSERT_TRUE(replacement != old);
    ASSERT_FALSE(s->nodeIsValid(old, old_serial));

    /* Prune the original first page and then the fresh middle replacement. */
    for (int k = 0; k < 2; k++) {
        ASSERT_TRUE(fillLastPageForTest(&*s));
        (void)growNode(&*s);
        ASSERT_TRUE(expectLivePageSerialsValidForTest(&*s));
    }
}

TEST(page_list, PageList_incremental_compression_restarts_after_earlier_replacement) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 3));

    (void)s->compress(CMode::incremental);
    (void)s->compress(CMode::incremental);
    ASSERT_TRUE(s->pages.first->isCompressed());
    ASSERT_TRUE(s->pages.first->next->isCompressed());

    /* Replace a page before the still-valid continuation marker. The list's
     * allocation serial changes even though the marker itself remains, so the
     * next step must restart and inspect the replacement. */
    Node *old_first = s->pages.first;
    const uint64_t old_serial = old_first->serial;
    Node *replacement;
    ASSERT_TRUE(s->increaseCapacity(old_first, PageList::IncreaseCapacity::grapheme_bytes, &replacement) ==
                PageList::IncreaseCapacityError::none);
    ASSERT_TRUE(replacement->serial != old_serial);
    ASSERT_FALSE(replacement->isCompressed());

    (void)s->compress(CMode::incremental);
    ASSERT_TRUE(replacement->isCompressed());
}

TEST(page_list, PageList_incremental_compression_keeps_progress_after_tail_growth) {
    const size_t max_inspected = PageList::incremental_compression_max_inspected;
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, max_inspected + 1));
    (void)s->compress(CMode::full);
    s->page_compression.reset();
    s->page_compression.markActivity();

    Node *expected_last = s->pages.first;
    for (size_t i = 1; i < max_inspected; i++) expected_last = expected_last->next;

    const CResult first = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == first);
    ASSERT_TRUE(expected_last->serial == s->page_compression.last_serial.value);

    /* Allocate a new page at the active tail between steps. It is after the
     * continuation marker and must not restart progress through cold history. */
    const uint64_t next_serial = s->page_serial;
    while (s->page_serial == next_serial) (void)growNode(&*s);
    const CResult continued = s->compress(CMode::incremental);
    ASSERT_TRUE(CResult::pending == continued);
    ASSERT_TRUE(s->page_compression.flags.verifying);
}

TEST(page_list, PageList_memory_stats_do_not_restore_compressed_pages) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 2));

    const PageList::MemoryStats before = s->memoryStats();
    ASSERT_TRUE(s->totalPages() == before.resident_pages);
    ASSERT_TRUE(0 == before.compressed_pages);
    ASSERT_TRUE(s->page_size == before.raw_bytes);
    ASSERT_TRUE(before.raw_bytes == before.resident_raw_bytes);
    ASSERT_TRUE(0 == before.decommitted_raw_bytes);
    ASSERT_TRUE(s->page_size == before.resident_backing_bytes);
    ASSERT_TRUE(0 == before.encoded_bytes);
    ASSERT_TRUE(before.resident_backing_bytes == before.estimatedResidentBytes());
    ASSERT_TRUE(0 == before.estimatedSavings());

    (void)s->compress(CMode::full);
    Node *first = s->pages.first;
    ASSERT_TRUE(Node::Storage::compressed == first->storage());
    ASSERT_TRUE(first->pageIfResident() == nullptr);

    const PageList::MemoryStats after = s->memoryStats();
    ASSERT_TRUE(first->isCompressed());
    ASSERT_TRUE(s->totalPages() == after.resident_pages + after.compressed_pages);
    ASSERT_TRUE(2 == after.compressed_pages);
    ASSERT_TRUE(s->page_size == after.raw_bytes);
    ASSERT_TRUE(after.raw_bytes == after.resident_raw_bytes + after.decommitted_raw_bytes);
    ASSERT_TRUE(after.resident_backing_bytes + after.encoded_bytes == after.estimatedResidentBytes());
    ASSERT_TRUE(after.decommitted_raw_bytes - after.encoded_bytes == after.estimatedSavings());

    const size_t first_raw_len = first->metadata()->memory_len;
    const size_t first_encoded_len = first->data.compressed.encoded_len;
    (void)first->page();
    ASSERT_TRUE(Node::Storage::resident == first->storage());
    ASSERT_TRUE(first->pageIfResident() != nullptr);

    const PageList::MemoryStats restored = s->memoryStats();
    ASSERT_TRUE(after.resident_pages + 1 == restored.resident_pages);
    ASSERT_TRUE(after.compressed_pages - 1 == restored.compressed_pages);
    ASSERT_TRUE(after.raw_bytes == restored.raw_bytes);
    ASSERT_TRUE(after.resident_raw_bytes + first_raw_len == restored.resident_raw_bytes);
    ASSERT_TRUE(after.decommitted_raw_bytes - first_raw_len == restored.decommitted_raw_bytes);
    ASSERT_TRUE(after.resident_backing_bytes + first_raw_len == restored.resident_backing_bytes);
    ASSERT_TRUE(after.encoded_bytes - first_encoded_len == restored.encoded_bytes);
}

TEST(page_list, PageList_preserved_page_keeps_compressed_storage) {
    const zigstd::Allocator alloc = talloc();
    ListHolder s(alloc, opts(80, 24));

    Node *node = s->pages.first;
    Page *resident = node->page();
    resident->dirty = true;
    *resident->getRowAndCell(3, 2).cell = page::Cell::init('X');

    /* Resident nodes can be borrowed without allocating an unnecessary copy. */
    {
        zigstd::FailingAllocator failing(alloc, 0);
        Node::PreservedPage preserved;
        ASSERT_TRUE(node->pagePreservingState(failing.allocator(), &preserved));
        ASSERT_TRUE(preserved.tag == Node::PreservedPage::Tag::borrowed);
        ASSERT_TRUE(resident == preserved.borrowed);
        ASSERT_FALSE(failing.has_induced_failure);
        preserved.deinit();
    }

    uint8_t *expected = dupeMem(resident);
    uint8_t *const retained_ptr = resident->memory;
    const size_t mem_len = resident->memory_len;

    ASSERT_TRUE(s->compressPage(node));
    const PageList::MemoryStats stats = s->memoryStats();
    const size_t enc_len = node->data.compressed.encoded_len;
    uint8_t *expected_encoded = (uint8_t *)malloc(enc_len);
    memcpy(expected_encoded, node->data.compressed.encoded, enc_len);

    /* Test decommit simulates physical reclamation by clearing the retained
     * mapping. A preserved page must decode elsewhere rather than restoring
     * it. */
    ASSERT_TRUE(allZero(node->metadata()->memory, mem_len));

    /* Preserved-page allocation is opportunistic for callers. Failure leaves
     * the node and its compressed representation untouched. */
    {
        zigstd::FailingAllocator failing(alloc, 0);
        Node::PreservedPage p;
        ASSERT_FALSE(node->pagePreservingState(failing.allocator(), &p));
    }
    ASSERT_TRUE(Node::Storage::compressed == node->storage());
    ASSERT_TRUE(statsEq(stats, s->memoryStats()));

    Node::PreservedPage preserved;
    ASSERT_TRUE(node->pagePreservingState(alloc, &preserved));
    ASSERT_TRUE(preserved.tag == Node::PreservedPage::Tag::owned);
    const Page *page_ = preserved.page();

    ASSERT_TRUE(page_->memory != retained_ptr);
    ASSERT_TRUE(memcmp(expected, page_->memory, mem_len) == 0);
    ASSERT_TRUE(page_->dirty);
    ASSERT_TRUE('X' == page_->getRowAndCell(3, 2).cell->contentCodepoint());

    /* The node still owns the same compressed representation, and neither its
     * storage accounting nor its discarded raw mapping changed while cloning. */
    ASSERT_TRUE(Node::Storage::compressed == node->storage());
    ASSERT_TRUE(statsEq(stats, s->memoryStats()));
    ASSERT_TRUE(retained_ptr == node->metadata()->memory);
    ASSERT_TRUE(allZero(node->metadata()->memory, mem_len));
    ASSERT_TRUE(memcmp(expected_encoded, node->data.compressed.encoded, enc_len) == 0);

    preserved.deinit();
    free(expected_encoded);
    free(expected);
}

TEST(page_list, PageList_memory_stats_include_unused_pool_backing) {
    ListHolder s(opts(80, 24));

    /* Pool allocation ownership is based on the requested layout fitting in a
     * standard item. The Page itself exposes only the initialized prefix. */
    Node *node = s->createPage(PageList::CreatePage(Capacity(1, 1)));
    ASSERT_TRUE(Node::Owned::pool == node->owned);
    ASSERT_TRUE(node->page()->memory_len < PageList::PagePool::item_size);
    node->page()->size.rows = 1;
    s->pages.append(node);
    s->total_rows += 1;

    const size_t raw_len = node->metadata()->memory_len;
    const PageList::MemoryStats before = s->memoryStats();
    ASSERT_TRUE(before.raw_bytes < s->page_size);
    ASSERT_TRUE(before.raw_bytes == before.resident_raw_bytes);
    ASSERT_TRUE(s->page_size == before.resident_backing_bytes);
    ASSERT_TRUE(s->page_size == before.estimatedResidentBytes());

    ASSERT_TRUE(s->compressPage(node));
    const size_t encoded_len = node->data.compressed.encoded_len;
    const PageList::MemoryStats compressed = s->memoryStats();
    ASSERT_TRUE(before.raw_bytes == compressed.raw_bytes);
    ASSERT_TRUE(before.resident_raw_bytes - raw_len == compressed.resident_raw_bytes);
    ASSERT_TRUE(raw_len == compressed.decommitted_raw_bytes);
    ASSERT_TRUE(before.resident_backing_bytes - raw_len == compressed.resident_backing_bytes);
    ASSERT_TRUE(encoded_len == compressed.encoded_bytes);
    ASSERT_TRUE(before.estimatedResidentBytes() - raw_len + encoded_len == compressed.estimatedResidentBytes());
}

TEST(page_list, PageList_does_not_compress_the_mixed_history_and_active_page) {
    ListHolder s(opts(80, 24));

    /* One additional row creates history, but the history and all active rows
     * still share the first page. The active boundary therefore has a
     * historical prefix and must remain resident as one indivisible mapping. */
    (void)growNode(&*s);
    const Pin active = s->getTopLeft(point::Tag::active);
    ASSERT_TRUE(s->pages.first == active.node);
    ASSERT_TRUE(active.y > 0);

    (void)s->compress(CMode::full);
    ASSERT_FALSE(s->pages.first->isCompressed());
}

TEST(page_list, PageList_compresses_only_complete_cold_history_pages) {
    const zigstd::Allocator alloc = talloc();

    /* More active rows than one page at these dimensions can hold ensures the
     * active area spans multiple nodes when the pass chooses its boundary. */
    const size::CellCountInt active_rows = (size::CellCountInt)(PageList::initialCapacity(80).rows + 1);
    ListHolder s(alloc, opts(80, active_rows));
    ASSERT_TRUE(growColdPagesForTest(&*s, 2));

    /* Move the active top into the boundary page so it has both a historical
     * prefix and active rows while the active area still spans later pages. */
    (void)growNode(&*s);

    const Pin active = s->getTopLeft(point::Tag::active);
    Node *active_node = active.node;
    ASSERT_TRUE(active.y > 0);
    ASSERT_TRUE(active_node != s->pages.last);

    size_t expected_compressed = 0;
    size_t expected_raw_bytes = 0;
    for (Node *node = s->pages.first; node; node = node->next) {
        if (node == active_node) break;
        expected_compressed += 1;
        expected_raw_bytes += node->metadata()->memory_len;
    }
    ASSERT_TRUE(2 == expected_compressed);

    Node *first = s->pages.first;
    *first->page()->getRowAndCell(0, 0).cell = page::Cell::init('X');
    uint8_t *expected = dupeMem(first->page());
    uint8_t *first_memory = first->page()->memory;
    const size_t page_size = s->page_size;

    (void)s->compress(CMode::full);
    const PageList::MemoryStats memory = s->memoryStats();
    ASSERT_TRUE(expected_compressed == memory.compressed_pages);
    ASSERT_TRUE(expected_raw_bytes == memory.decommitted_raw_bytes);
    ASSERT_TRUE(memory.encoded_bytes < memory.decommitted_raw_bytes);
    ASSERT_TRUE(page_size == s->page_size);

    size_t actual_encoded_bytes = 0;
    for (Node *node = s->pages.first; node; node = node->next) {
        if (node == active_node) break;
        ASSERT_TRUE(node->isCompressed());
        actual_encoded_bytes += node->data.compressed.encoded_len;
    }
    ASSERT_TRUE(actual_encoded_bytes == memory.encoded_bytes);
    for (Node *node = active_node; node; node = node->next) {
        ASSERT_FALSE(node->isCompressed());
    }

    /* Restoring the oldest page preserves both the mapping identity and all
     * of its bytes even though the pass discarded its physical pages. */
    ASSERT_TRUE(first_memory == first->metadata()->memory);
    ASSERT_TRUE(memcmp(expected, first->page()->memory, first->page()->memory_len) == 0);
    ASSERT_TRUE(first_memory == first->page()->memory);
    free(expected);
}

TEST(page_list, PageList_lazily_restores_compressed_history_made_active_by_resize) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 1));

    Node *first = s->pages.first;
    *first->page()->getRowAndCell(0, 0).cell = page::Cell::init('X');
    uint8_t *memory_ptr = first->page()->memory;
    const size_t memory_len = first->page()->memory_len;
    const size_t page_size = s->page_size;

    (void)s->compress(CMode::full);
    ASSERT_TRUE(first->isCompressed());

    /* Pull all scrollback into the active area by making the viewport as tall
     * as the complete screen. A row-only resize needs only page metadata, so
     * the newly active page can remain compressed until its contents are used. */
    const size::CellCountInt all_rows = (size::CellCountInt)s->total_rows;
    {
        PageList::Resize r;
        r.rows = all_rows;
        ASSERT_TRUE(s->resize(r));
    }
    const Pin active = s->getTopLeft(point::Tag::active);
    ASSERT_TRUE(first == active.node);
    ASSERT_TRUE(0 == active.y);
    ASSERT_TRUE(first->isCompressed());
    ASSERT_TRUE(page_size == s->page_size);

    /* The compression pass must not reconsider the node now that it is active.
     * Content access follows the normal page boundary, which recommits and
     * restores the retained mapping before returning the cell. */
    (void)s->compress(CMode::full);
    ASSERT_TRUE(first->isCompressed());
    const PageList::Cell cell = s->getCell(Point::active()).value;
    ASSERT_TRUE('X' == cell.cell->contentCodepoint());
    ASSERT_FALSE(first->isCompressed());
    ASSERT_TRUE(memory_ptr == first->page()->memory);
    ASSERT_TRUE(memory_len == first->page()->memory_len);
    ASSERT_TRUE(page_size == s->page_size);
}

TEST(page_list, PageList_full_and_incremental_compression_skip_a_spanning_viewport) {
    ListHolder full(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*full, 3));

    ListHolder incremental(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*incremental, 3));

    /* Start near the end of the first page so the viewport intersects both
     * the first and second historical page mappings. */
    Node *first = full->pages.first;
    const size_t overlap_rows = full->rows / 2;
    const size_t viewport_row = first->rows() - overlap_rows;
    full->scroll(PageList::Scroll::rowAt(viewport_row));
    incremental->scroll(PageList::Scroll::rowAt(viewport_row));
    ASSERT_TRUE(full->getTopLeft(point::Tag::viewport).node !=
                full->getBottomRight(point::Tag::viewport).value.node);
    Node *second = first->next;
    ASSERT_TRUE(first == full->getTopLeft(point::Tag::viewport).node);
    ASSERT_TRUE(second == full->getBottomRight(point::Tag::viewport).value.node);

    (void)full->compress(CMode::full);
    (void)incremental->compress(CMode::drain);
    ASSERT_TRUE(statsEq(full->memoryStats(), incremental->memoryStats()));
    ASSERT_FALSE(first->isCompressed());
    ASSERT_FALSE(second->isCompressed());

    Node *full_active = full->getTopLeft(point::Tag::active).node;
    Node *incremental_active = incremental->getTopLeft(point::Tag::active).node;
    Node *full_node = full->pages.first;
    Node *incremental_node = incremental->pages.first;
    while (full_node != full_active) {
        ASSERT_TRUE(full_node->isCompressed() == incremental_node->isCompressed());

        full_node = full_node->next;
        incremental_node = incremental_node->next;
    }
    ASSERT_TRUE(incremental_active == incremental_node);

    PageList::CompressionIterator eligible = PageList::CompressionIterator::init(&*full);
    size_t compressed_pages = 0;
    while (Node *node = eligible.next()) {
        compressed_pages += 1;
        ASSERT_TRUE(node->isCompressed());
    }
    ASSERT_TRUE(compressed_pages > 0);
}

TEST(page_list, PageList_cold_compression_continues_after_an_incompressible_page) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(growColdPagesForTest(&*s, 2));

    Node *first = s->pages.first;
    Node *second = first->next;
    uint8_t *original = dupeMem(first->page());
    zigstd::DefaultPrng prng = zigstd::DefaultPrng::init(0x434F4C4450414745ull);
    zigstd::Random(&prng).bytes(first->page()->memory, first->page()->memory_len);

    const size_t page_size = s->page_size;
    (void)s->compress(CMode::full);
    const PageList::MemoryStats memory = s->memoryStats();
    ASSERT_TRUE(1 == memory.compressed_pages);
    ASSERT_FALSE(first->isCompressed());
    ASSERT_TRUE(second->isCompressed());
    ASSERT_TRUE(second->metadata()->memory_len == memory.decommitted_raw_bytes);
    ASSERT_TRUE(memory.encoded_bytes < memory.decommitted_raw_bytes);
    ASSERT_TRUE(page_size == s->page_size);

    /* Failed resident candidates are deliberately retried on later passes,
     * while the successful page remains compressed and is skipped. */
    (void)s->compress(CMode::full);
    ASSERT_TRUE(statsEq(memory, s->memoryStats()));

    /* Wisp: restore the valid page bytes before teardown. */
    memcpy(first->page()->memory, original, first->page()->memory_len);
    free(original);
}

TEST(page_list, PageList_compression_restores_through_page_access) {
    const zigstd::Allocator alloc = talloc();
    ListHolder s(alloc, opts(80, 24));

    Node *node = s->pages.first;
    Page *page = node->page();
    page->dirty = true;
    *page->getRowAndCell(3, 2).cell = page::Cell::init('X');

    uint8_t *expected = dupeMem(page);
    uint8_t *memory_ptr = page->memory;
    const size_t memory_len = page->memory_len;
    const size_t page_size = s->page_size;

    ASSERT_TRUE(s->compressPage(node));
    ASSERT_TRUE(node->isCompressed());
    ASSERT_TRUE(24 == node->rows());
    ASSERT_TRUE(80 == node->cols());
    ASSERT_TRUE(memory_ptr == node->metadata()->memory);
    ASSERT_TRUE(memory_len == node->metadata()->memory_len);
    ASSERT_TRUE(page_size == s->page_size);

    /* Pin access restores the page without changing its retained mapping. */
    const Pin page_pin = Pin::at(node, 3, 2);
    ASSERT_TRUE('X' == page_pin.rowAndCell().cell->contentCodepoint());
    ASSERT_FALSE(node->isCompressed());
    ASSERT_TRUE(memory_ptr == node->page()->memory);
    ASSERT_TRUE(memcmp(expected, node->page()->memory, memory_len) == 0);
    ASSERT_TRUE(node->page()->dirty);

    /* Recompressing exercises reuse of the page-pool scratch item. Page
     * iterator chunks also restore before exposing row memory. */
    ASSERT_TRUE(s->compressPage(node));
    PageList::PageIterator page_it = Pin(node).pageIterator(PageList::Direction::right_down, Maybe<Pin>());
    PageList::Chunk chunk;
    ASSERT_TRUE(page_it.next(&chunk));
    size_t chunk_len;
    (void)chunk.rows(&chunk_len);
    ASSERT_TRUE(node->rows() == chunk_len);
    ASSERT_FALSE(node->isCompressed());
    ASSERT_TRUE(memcmp(expected, node->page()->memory, memory_len) == 0);

    /* Read-only PageList operations restore through the same boundary. */
    ASSERT_TRUE(s->compressPage(node));
    PageList cloned;
    ASSERT_TRUE(s->clone(alloc, PageList::Clone(Point::screen()), &cloned) == page::PageError::none);
    ASSERT_FALSE(node->isCompressed());
    ASSERT_TRUE('X' == cloned.pages.first->page()->getRowAndCell(3, 2).cell->contentCodepoint());
    cloned.deinit();
    free(expected);
}

TEST(page_list, PageList_compression_uses_temporary_scratch_for_oversized_pages) {
    const zigstd::Allocator alloc = talloc();
    ListHolder s(alloc, opts(80, 24));

    Node *node = s->pages.first;
    while (node->page()->memory_len <= PageList::std_size) {
        ASSERT_TRUE(s->increaseCapacity(node, PageList::IncreaseCapacity::grapheme_bytes, &node) ==
                    PageList::IncreaseCapacityError::none);
    }

    uint8_t *expected = dupeMem(node->page());
    uint8_t *memory_ptr = node->page()->memory;
    const size_t memory_len = node->page()->memory_len;
    const size_t page_size = s->page_size;

    ASSERT_TRUE(s->compressPage(node));
    ASSERT_TRUE(node->isCompressed());
    ASSERT_TRUE(page_size == s->page_size);
    ASSERT_TRUE(memory_ptr == node->metadata()->memory);
    ASSERT_TRUE(memory_len == node->metadata()->memory_len);

    ASSERT_TRUE(memcmp(expected, node->page()->memory, memory_len) == 0);
    ASSERT_FALSE(node->isCompressed());
    ASSERT_TRUE(memory_ptr == node->page()->memory);
    free(expected);
}

TEST(page_list, PageList_compression_leaves_incompressible_pages_resident) {
    const zigstd::Allocator alloc = talloc();
    ListHolder s(alloc, opts(80, 24));

    Node *node = s->pages.first;
    uint8_t *original = dupeMem(node->page());

    zigstd::DefaultPrng prng = zigstd::DefaultPrng::init(0x504147454C495354ull);
    zigstd::Random(&prng).bytes(node->page()->memory, node->page()->memory_len);
    const size_t page_size = s->page_size;

    ASSERT_FALSE(s->compressPage(node));
    ASSERT_FALSE(node->isCompressed());
    ASSERT_TRUE(page_size == s->page_size);

    memcpy(node->page()->memory, original, node->page()->memory_len);
    free(original);
}

TEST(page_list, PageList_reset_discards_malformed_compressed_data) {
    ListHolder s(opts(80, 24));

    Node *node = s->pages.first;
    ASSERT_TRUE(s->compressPage(node));
    memset(node->data.compressed.encoded, 0xFF, node->data.compressed.encoded_len);

    s->reset();
    ASSERT_FALSE(s->pages.first->isCompressed());
    ASSERT_TRUE(1 == s->totalPages());
}

TEST(page_list, PageList_deinit_discards_malformed_compressed_data) {
    PageList s;
    ASSERT_TRUE(PageList::init(talloc(), opts(80, 24), &s));
    Node *node = s.pages.first;
    ASSERT_TRUE(s.compressPage(node));
    memset(node->data.compressed.encoded, 0xFF, node->data.compressed.encoded_len);

    s.deinit();
}

typedef PageList::Scrollbar Scrollbar;

static bool sbEq(const Scrollbar &a, size_t total, size_t offset, size_t len) {
    return a.total == total && a.offset == offset && a.len == len;
}

/* expectEqual on Pin compares every field. */
static bool pinEq(const Pin &a, const Pin &b) { return a.eql(b) && a.garbage == b.garbage; }

static size::CellCountInt stdMaxCols() {
    size::CellCountInt c;
    const bool ok = page::std_capacity().maxCols(&c);
    assert(ok);
    (void)ok;
    return c;
}

static Capacity stdAdjust(size::CellCountInt cols) {
    Capacity c;
    const bool ok = page::std_capacity().adjust(Capacity::Adjustment::withCols(cols), &c);
    assert(ok);
    (void)ok;
    return c;
}

static Point cellScreenPoint(PageList &s, const Point &pt) { return s.getCell(pt).value.screenPoint(); }

TEST(page_list, PageList_prune_reuses_malformed_compressed_page_memory) {
    ListHolder s(opts(80, 24, 2 * PageList::PagePool::item_size));

    /* Allocate the second page so the first one can be pruned and reused. */
    while (s->pages.first == s->pages.last) (void)growNode(&*s);
    Node *first = s->pages.first;
    ASSERT_TRUE(s->compressPage(first));
    memset(first->data.compressed.encoded, 0xFF, first->data.compressed.encoded_len);

    bool reused = false;
    const size_t growth_limit = (size_t)s->pages.last->capacity().rows + 1;
    for (size_t i = 0; i < growth_limit; i++) {
        if (Node *new_node = growNode(&*s)) {
            if (new_node == first) {
                reused = true;
                break;
            }
        }
    }

    ASSERT_TRUE(reused);
    ASSERT_TRUE(first == s->pages.last);
    ASSERT_FALSE(first->isCompressed());
    ASSERT_TRUE(1 == first->rows());
    first->page()->assertIntegrity();
}

TEST(page_list, PageList) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->viewport == Viewport::active);
    ASSERT_TRUE(s->pages.first != nullptr);
    ASSERT_TRUE((size_t)s->rows == s->totalRows());

    /* Initial total rows should be our row count */
    ASSERT_TRUE(s->rows == s->total_rows);

    /* Our viewport pin must be defined. It isn't used until the
     * viewport is a pin but it prevents undefined access on clone. */
    ASSERT_TRUE(s->viewport_pin->node == s->pages.first);

    /* Active area should be the top */
    ASSERT_TRUE(pinEq(Pin(s->pages.first, 0, 0), s->getTopLeft(point::Tag::active)));

    /* Scrollbar should be where we expect it */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->rows, 0, s->rows));
}

TEST(page_list, PageList_init_error) {
    /* Test every failure point in `init` and ensure that we don't
     * leak memory (testing.allocator verifies) since we're exiting early. */
    const PageList::InitTw init_tags[] = {PageList::InitTw::init_memory_pool, PageList::InitTw::init_pages,
                                          PageList::InitTw::viewport_pin, PageList::InitTw::viewport_pin_track};
    for (PageList::InitTw tag : init_tags) {
        typedef PageList::init_tw tw;
        tw::errorAlways(tag, AllocTw::OutOfMemory);
        PageList s;
        ASSERT_FALSE(PageList::init(talloc(), opts(80, 24), &s));
        (void)tw::end(tripwire::ResetMode::reset);
    }

    /* init calls initPages transitively, so let's check that if
     * any failures happen in initPages, we also don't leak memory. */
    const PageList::InitPagesTw page_tags[] = {PageList::InitPagesTw::page_node, PageList::InitPagesTw::page_buf_std,
                                               PageList::InitPagesTw::page_buf_non_std};
    for (PageList::InitPagesTw tag : page_tags) {
        typedef PageList::initPages_tw tw;
        tw::errorAlways(tag, AllocTw::OutOfMemory);

        const size::CellCountInt cols =
            tag == PageList::InitPagesTw::page_buf_std ? 80 : (size::CellCountInt)(stdMaxCols() + 1);
        PageList s;
        ASSERT_FALSE(PageList::init(talloc(), opts(cols, 24), &s));
        (void)tw::end(tripwire::ResetMode::reset);
    }

    /* Try non-standard pages since they don't go in our pool. */
    {
        typedef PageList::initPages_tw tw;
        tw::errorAfter(PageList::InitPagesTw::page_buf_non_std, AllocTw::OutOfMemory, 1);
        PageList s;
        ASSERT_FALSE(PageList::init(talloc(),
                                    opts((size::CellCountInt)(stdMaxCols() + 1),
                                         (size::CellCountInt)(page::std_capacity().rows + 1)),
                                    &s));
        (void)tw::end(tripwire::ResetMode::reset);
    }
}

TEST(page_list, PageList_init_rows_across_two_pages) {
    /* Find a cap that makes it so that rows don't fit on one page. */
    const size::CellCountInt rows = 100;
    Capacity cap = stdAdjust(50);
    while (cap.rows >= rows) cap = stdAdjust((size::CellCountInt)(cap.cols + 50));

    /* Init */
    ListHolder s(opts(cap.cols, rows));
    ASSERT_TRUE(s->viewport == Viewport::active);
    ASSERT_TRUE(s->pages.first != nullptr);
    ASSERT_TRUE((size_t)s->rows == s->totalRows());

    /* Initial total rows should be our row count */
    ASSERT_TRUE(s->rows == s->total_rows);

    /* Scrollbar should be where we expect it */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->rows, 0, s->rows));
}

TEST(page_list, PageList_init_more_than_max_cols) {
    /* Initialize with more columns than we can fit in our standard
     * capacity. This is going to force us to go to a non-standard page
     * immediately. */
    ListHolder s(opts((size::CellCountInt)(stdMaxCols() + 1), 80));
    ASSERT_TRUE(s->viewport == Viewport::active);
    ASSERT_TRUE((size_t)s->rows == s->totalRows());

    /* We expect a single, non-standard page */
    ASSERT_TRUE(s->pages.first != nullptr);
    ASSERT_TRUE(s->pages.first->page()->memory_len > PageList::std_size);

    /* Initial total rows should be our row count */
    ASSERT_TRUE(s->rows == s->total_rows);

    /* Scrollbar should be where we expect it */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->rows, 0, s->rows));
}

TEST(page_list, PageList_pointFromPin_active_no_history) {
    ListHolder s(opts(80, 24));

    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 0, 0)), Point::active(0, 0)));
    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 2, 4)), Point::active(4, 2)));
}

TEST(page_list, PageList_pointFromPin_active_with_history) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(30));

    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 30, 2)), Point::active(2, 0)));

    /* In history, invalid */
    ASSERT_FALSE(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 21, 2)).has);
}

static void growPagesPaused(PageList &s, size_t n) {
    Node *cur_page = s.pages.last;
    cur_page->page()->pauseIntegrityChecks(true);
    for (size_t i = 0; i < n; i++) {
        if (Node *new_page = growNode(&s)) {
            cur_page->page()->pauseIntegrityChecks(false);
            cur_page = new_page;
            cur_page->page()->pauseIntegrityChecks(true);
        }
    }
    cur_page->page()->pauseIntegrityChecks(false);
}

TEST(page_list, PageList_pointFromPin_active_from_prior_page) {
    ListHolder s(opts(80, 24));
    /* Grow so we take up at least 5 pages. */
    Page *page = s->pages.last->page();
    growPagesPaused(*s, (size_t)page->capacity.rows * 5);

    ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::active, Pin(s->pages.last, 0, 2)), Point::active(2, 0)));

    /* Prior page */
    ASSERT_FALSE(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 0, 0)).has);
}

TEST(page_list, PageList_pointFromPin_traverse_pages) {
    ListHolder s(opts(80, 24));

    /* Grow so we take up at least 2 pages. */
    Page *page = s->pages.last->page();
    const size_t page_cap = page->capacity.rows;
    growPagesPaused(*s, page_cap * 2);

    {
        const size_t pages = s->totalPages();
        const size_t expected_y = page_cap * (pages - 2) + 5;

        ASSERT_TRUE(pointEq(s->pointFromPin(point::Tag::screen, Pin(s->pages.last->prev, 5, 2)),
                            Point::screen(2, (uint32_t)expected_y)));
    }

    /* Prior page */
    ASSERT_FALSE(s->pointFromPin(point::Tag::active, Pin(s->pages.first, 0, 0)).has);
}

TEST(page_list, PageList_pointFromPin_rejects_overflowing_screen_coordinate) {
    /* Use maximum-height metadata-only pages to model a valid scrollback just
     * beyond the u32 coordinate range without allocating their backing cells. */
    const size_t page_count = 65539;
    const size::CellCountInt rows_per_page = 0xFFFF;
    Node *nodes = (Node *)calloc(page_count, sizeof(Node));

    for (size_t i = 0; i < page_count; i++) {
        Node *node = &nodes[i];
        node->prev = i > 0 ? &nodes[i - 1] : nullptr;
        node->next = i + 1 < page_count ? &nodes[i + 1] : nullptr;
        node->data.tag = Node::Data::Tag::resident;
        node->serial = i;
        node->owned = Node::Owned::heap;
        node->data.resident().size.cols = 1;
        node->data.resident().size.rows = rows_per_page;
    }

    PageList s;
    s.pages.first = &nodes[0];
    s.pages.last = &nodes[page_count - 1];

    ASSERT_FALSE(s.pointFromPin(point::Tag::screen, Pin(&nodes[page_count - 1], 0, 0)).has);
    free(nodes);
}

TEST(page_list, PageList_active_after_grow) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE((size_t)s->rows == s->totalRows());

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE((size_t)s->rows + 10 == s->totalRows());

    /* Make sure all points make sense */
    ASSERT_TRUE(cellScreenPoint(*s, Point::viewport()).eql(Point::screen(0, 10)));
    ASSERT_TRUE(cellScreenPoint(*s, Point::screen()).eql(Point::screen(0, 0)));
    ASSERT_TRUE(cellScreenPoint(*s, Point::active()).eql(Point::screen(0, 10)));

    /* Scrollbar should be in the active area */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 10, s->rows));
}

TEST(page_list, PageList_grow_allows_exceeding_max_size_for_active_area) {
    /* Setup our initial page so that we fully take up one page. */
    const Capacity cap = stdAdjust(5);
    ListHolder s(opts(5, cap.rows, (size_t)0));
    ASSERT_TRUE((size_t)s->rows == s->totalRows());

    /* Grow once because we guarantee at least two pages of
     * capacity so we want to get to that. */
    (void)growNode(&*s);
    const size_t start_pages = s->totalPages();
    ASSERT_TRUE(start_pages >= 2);

    /* Surgically modify our pages so that they have a smaller size. */
    {
        for (Node *page = s->pages.first; page; page = page->next) {
            page->page()->size.rows = 1;
            page->page()->capacity.rows = 1;
        }

        /* Avoid integrity check failures */
        s->total_rows = s->totalRows();
    }

    /* Grow our row and ensure we don't prune pages because we need
     * enough for the active area. */
    (void)growNode(&*s);
    ASSERT_TRUE(start_pages + 1 == s->totalPages());
}

TEST(page_list, PageList_grow_prune_required_with_a_single_page) {
    /* Need scrollback > 0 to have a scrollbar to test */
    ListHolder s(opts(80, 24));

    /* This block is all test setup. There is nothing required about this
     * behavior during a refactor. This is setting up a scenario that is
     * possible to trigger a bug (#2280). */
    {
        /* Increase our capacity until our page is larger than the standard size.
         * This is important because it triggers a scenario where our calculated
         * minSize() which is supposed to accommodate 2 pages is no longer true. */
        for (;;) {
            const Page::Layout layout = Page::layout(s->pages.first->capacity());
            if (layout.total_size > PageList::std_size) break;
            Node *n;
            ASSERT_TRUE(s->increaseCapacity(s->pages.first, PageList::IncreaseCapacity::grapheme_bytes, &n) ==
                        PageList::IncreaseCapacityError::none);
        }
        ASSERT_TRUE(s->pages.first != nullptr);
        ASSERT_TRUE(s->pages.first == s->pages.last);
    }

    /* Figure out the remaining number of rows. This is the amount that
     * can be added to the current page before we need to allocate a new
     * page. */
    const size_t rem = (size_t)s->pages.first->capacity().rows - s->pages.first->rows();
    for (size_t i = 0; i < rem; i++) ASSERT_TRUE(growNode(&*s) == nullptr);

    /* The next one we add will trigger a new page. */
    Node *new_ = growNode(&*s);
    ASSERT_TRUE(new_ != nullptr);
    ASSERT_TRUE(new_ != s->pages.first);

    /* Scrollbar should be in the active area */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scrollbar_with_max_size_0_after_grow) {
    ListHolder s(opts(80, 24, (size_t)0));

    /* Grow some rows (simulates normal terminal output) */
    ASSERT_TRUE(s->growRows(10));

    const Scrollbar sb = s->scrollbar();

    /* With no scrollback (max_size = 0), total should equal rows */
    ASSERT_TRUE(s->rows == sb.total);

    /* With no scrollback, offset should be 0 (nowhere to scroll back to) */
    ASSERT_TRUE(0 == sb.offset);
}

TEST(page_list, PageList_scroll_with_max_size_0_no_history) {
    ListHolder s(opts(80, 24, (size_t)0));

    ASSERT_TRUE(s->growRows(10));

    /* Remember initial viewport position */
    const Point pt_before = cellScreenPoint(*s, Point::viewport());

    /* Try to scroll backwards into "history" - should be no-op */
    s->scroll(PageList::Scroll::deltaRow(-5));
    ASSERT_TRUE(s->viewport == Viewport::active);

    /* Scroll to top - should also be no-op with no scrollback */
    s->scroll(PageList::Scroll::top());
    const Point pt_after = cellScreenPoint(*s, Point::viewport());
    ASSERT_TRUE(pt_before.eql(pt_after));
}

TEST(page_list, PageList_scroll_top) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(cellScreenPoint(*s, Point::viewport()).eql(Point::screen(0, 10)));

    s->scroll(PageList::Scroll::top());

    ASSERT_TRUE(cellScreenPoint(*s, Point::viewport()).eql(Point::screen(0, 0)));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 0, s->rows));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(cellScreenPoint(*s, Point::viewport()).eql(Point::screen(0, 0)));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 0, s->rows));

    s->scroll(PageList::Scroll::active());
    ASSERT_TRUE(cellScreenPoint(*s, Point::viewport()).eql(Point::screen(0, 20)));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows, s->rows));
}

/* Wisp: `s.getCell(.{ .viewport = .{} }).?.screenPoint() == .{ .screen = .{ .x = 0, .y = y } }` */
static bool vpAt(PageList &s, uint32_t y) { return cellScreenPoint(s, Point::viewport()).eql(Point::screen(0, y)); }
typedef PageList::Scroll Scroll;

TEST(page_list, PageList_scroll_delta_row_back) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(vpAt(*s, 10));

    s->scroll(Scroll::deltaRow(-1));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows - 1, s->rows));

    ASSERT_TRUE(vpAt(*s, 9));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 9));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows - 11, s->rows));

    s->scroll(Scroll::deltaRow(-1));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows - 12, s->rows));
}

TEST(page_list, PageList_scroll_delta_row_back_overflow) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(vpAt(*s, 10));

    s->scroll(Scroll::deltaRow(-100));

    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 0, s->rows));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 0, s->rows));
}

TEST(page_list, PageList_scroll_minimum_row_delta) {
    ListHolder s(opts(10, 3));

    /* Create one row of history so scrolling all the way back has an
     * observable result. */
    ASSERT_TRUE(s->growRows(1));
    s->scroll(Scroll::deltaRow(PTRDIFF_MIN));

    ASSERT_TRUE(Viewport::top == s->viewport);
}

TEST(page_list, PageList_scroll_delta_row_forward) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(vpAt(*s, 10));

    s->scroll(Scroll::top());
    s->scroll(Scroll::deltaRow(2));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 2, s->rows));

    ASSERT_TRUE(vpAt(*s, 2));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 2));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 2, s->rows));
}

TEST(page_list, PageList_scroll_delta_row_forward_into_active) {
    ListHolder s(opts(80, 24));

    s->scroll(Scroll::deltaRow(2));

    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scroll_delta_row_back_without_space_preserves_active) {
    ListHolder s(opts(80, 24));
    s->scroll(Scroll::deltaRow(-1));

    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scroll_to_pin) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    s->scroll(Scroll::pinAt(s->pin(Point::screen(2, 4)).value));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 4, s->rows));

    ASSERT_TRUE(vpAt(*s, 4));

    s->scroll(Scroll::pinAt(s->pin(Point::screen(2, 5)).value));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 5, s->rows));

    ASSERT_TRUE(vpAt(*s, 5));
}

TEST(page_list, PageList_scroll_to_pin_in_active) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    s->scroll(Scroll::pinAt(s->pin(Point::screen(2, 30)).value));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), s->total_rows - s->rows, s->rows));

    ASSERT_TRUE(vpAt(*s, 10));
}

TEST(page_list, PageList_scroll_to_pin_at_top) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    s->scroll(Scroll::pinAt(s->pin(Point::screen(2, 0)).value));

    ASSERT_TRUE(s->viewport == Viewport::top);

    ASSERT_TRUE(sbEq(s->scrollbar(), s->totalRows(), 0, s->rows));

    ASSERT_TRUE(vpAt(*s, 0));
}

TEST(page_list, PageList_scroll_to_row_0) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(vpAt(*s, 10));

    s->scroll(Scroll::rowAt(0));
    ASSERT_TRUE(s->viewport == Viewport::top);

    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 0, s->rows));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 0, s->rows));
}

TEST(page_list, PageList_scroll_to_row_in_scrollback) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(20));

    ASSERT_TRUE(vpAt(*s, 20));

    s->scroll(Scroll::rowAt(5));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 5, s->rows));

    ASSERT_TRUE(vpAt(*s, 5));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 5));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 5, s->rows));
}

TEST(page_list, PageList_scroll_to_row_in_middle) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(50));

    const size_t total = s->total_rows;
    const size_t midpoint = total / 2;
    s->scroll(Scroll::rowAt(midpoint));

    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, midpoint, s->rows));

    ASSERT_TRUE(vpAt(*s, (uint32_t)midpoint));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, (uint32_t)midpoint));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, midpoint, s->rows));
}

TEST(page_list, PageList_scroll_to_row_at_active_boundary) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(20));

    const size_t active_start = s->total_rows - s->rows;

    s->scroll(Scroll::rowAt(active_start));

    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(vpAt(*s, (uint32_t)active_start));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));

    ASSERT_TRUE(s->growRows(10));

    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scroll_to_row_beyond_active) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(10));

    s->scroll(Scroll::rowAt(1000));

    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(vpAt(*s, 10));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scroll_to_row_without_scrollback) {
    ListHolder s(opts(80, 24));

    s->scroll(Scroll::rowAt(5));

    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(vpAt(*s, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_scroll_to_row_then_delta) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(30));

    s->scroll(Scroll::rowAt(10));

    ASSERT_TRUE(s->viewport == Viewport::pin);

    ASSERT_TRUE(vpAt(*s, 10));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 10, s->rows));

    s->scroll(Scroll::deltaRow(5));

    ASSERT_TRUE(s->viewport == Viewport::pin);

    ASSERT_TRUE(vpAt(*s, 15));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 15, s->rows));

    s->scroll(Scroll::deltaRow(-3));

    ASSERT_TRUE(s->viewport == Viewport::pin);

    ASSERT_TRUE(vpAt(*s, 12));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 12, s->rows));
}

TEST(page_list, PageList_scroll_to_row_with_cache_fast_path_down) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(50));

    s->scroll(Scroll::rowAt(10));

    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 10, s->rows));

    ASSERT_TRUE(vpAt(*s, 10));

    /* Verify cache is populated */
    ASSERT_TRUE(s->viewport_pin_row_offset.has);
    ASSERT_TRUE(10 == s->viewport_pin_row_offset.value);

    /* Now scroll to a different row - this should use the fast path */
    s->scroll(Scroll::rowAt(20));

    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 20, s->rows));

    ASSERT_TRUE(vpAt(*s, 20));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 20));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 20, s->rows));
}

TEST(page_list, PageList_scroll_to_row_with_cache_fast_path_up) {
    ListHolder s(opts(80, 24));
    ASSERT_TRUE(s->growRows(50));

    s->scroll(Scroll::rowAt(30));

    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 30, s->rows));

    ASSERT_TRUE(vpAt(*s, 30));

    /* Verify cache is populated */
    ASSERT_TRUE(s->viewport_pin_row_offset.has);
    ASSERT_TRUE(30 == s->viewport_pin_row_offset.value);

    /* Now scroll up to a different row - this should use the fast path */
    s->scroll(Scroll::rowAt(15));

    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 15, s->rows));

    ASSERT_TRUE(vpAt(*s, 15));

    ASSERT_TRUE(s->growRows(10));
    ASSERT_TRUE(vpAt(*s, 15));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 15, s->rows));
}

TEST(page_list, PageList_scroll_clear) {
    ListHolder s(opts(80, 24));

    *s->getCell(Point::active(0, 0)).value.cell = page::Cell::init('A');
    *s->getCell(Point::active(0, 1)).value.cell = page::Cell::init('A');

    ASSERT_TRUE(s->scrollClear());

    ASSERT_TRUE(vpAt(*s, 2));
}

static void setPrompt(Page *page, size_t y, page::Row::SemanticPrompt sp) {
    page->getRowAndCell(0, y).row->setSemanticPrompt(sp);
}

TEST(page_list, PageList_jump_zero_prompts) {
    ListHolder s(opts(5, 3));
    ASSERT_TRUE(s->growRows(3));
    ASSERT_TRUE(s->pages.first == s->pages.last);
    Page *page = s->pages.first->page();
    setPrompt(page, 1, page::Row::SemanticPrompt::prompt);
    setPrompt(page, 5, page::Row::SemanticPrompt::prompt);

    s->scroll(Scroll::deltaPrompt(0));
    ASSERT_TRUE(s->viewport == Viewport::active);

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
}

TEST(page_list, PageList_jump_minimum_prompt_delta) {
    ListHolder s(opts(10, 3));

    s->scroll(Scroll::deltaPrompt(PTRDIFF_MIN));
    ASSERT_TRUE(Viewport::active == s->viewport);
}

static bool viewportScreenIs(PageList &s, uint32_t y) {
    return pointEq(s.pointFromPin(point::Tag::screen, s.pin(Point::viewport()).value), Point::screen(0, y));
}

TEST(page_list, Screen_jump_back_one_prompt) {
    ListHolder s(opts(5, 3));
    ASSERT_TRUE(s->growRows(3));
    ASSERT_TRUE(s->pages.first == s->pages.last);
    Page *page = s->pages.first->page();
    setPrompt(page, 1, page::Row::SemanticPrompt::prompt);
    setPrompt(page, 5, page::Row::SemanticPrompt::prompt);

    /* Jump back */
    {
        s->scroll(Scroll::deltaPrompt(-1));
        ASSERT_TRUE(s->viewport == Viewport::pin);
        ASSERT_TRUE(viewportScreenIs(*s, 1));

        ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 1, s->rows));
    }
    {
        s->scroll(Scroll::deltaPrompt(-1));
        ASSERT_TRUE(s->viewport == Viewport::pin);
        ASSERT_TRUE(viewportScreenIs(*s, 1));

        ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, 1, s->rows));
    }

    /* Jump forward */
    {
        s->scroll(Scroll::deltaPrompt(1));
        ASSERT_TRUE(s->viewport == Viewport::active);
        ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
    }
    {
        s->scroll(Scroll::deltaPrompt(1));
        ASSERT_TRUE(s->viewport == Viewport::active);
        ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, s->total_rows - s->rows, s->rows));
    }
}

TEST(page_list, Screen_jump_forward_prompt_skips_multiline_continuation) {
    ListHolder s(opts(5, 3));
    ASSERT_TRUE(s->growRows(7));

    /* Multiline prompt on rows 1-3. */
    s->pin(Point::screen(0, 1)).value.rowAndCell().row->setSemanticPrompt(page::Row::SemanticPrompt::prompt);
    s->pin(Point::screen(0, 2))
        .value.rowAndCell()
        .row->setSemanticPrompt(page::Row::SemanticPrompt::prompt_continuation);
    s->pin(Point::screen(0, 3))
        .value.rowAndCell()
        .row->setSemanticPrompt(page::Row::SemanticPrompt::prompt_continuation);

    /* Next prompt after command output. */
    s->pin(Point::screen(0, 6)).value.rowAndCell().row->setSemanticPrompt(page::Row::SemanticPrompt::prompt);

    /* Starting at the first prompt line should jump to the next prompt,
     * not to continuation lines. */
    s->scroll(Scroll::rowAt(1));
    s->scroll(Scroll::deltaPrompt(1));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(viewportScreenIs(*s, 6));

    /* Starting in the middle of continuation lines should also jump to
     * the next prompt. */
    s->scroll(Scroll::rowAt(2));
    s->scroll(Scroll::deltaPrompt(1));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(viewportScreenIs(*s, 6));
}

TEST(page_list, PageList_grow_fit_in_capacity) {
    ListHolder s(opts(80, 24));

    /* So we know we're using capacity to grow */
    Page *last = s->pages.last->page();
    ASSERT_TRUE(last->size.rows < last->capacity.rows);

    /* Grow */
    ASSERT_TRUE(growNode(&*s) == nullptr);
    ASSERT_TRUE(cellScreenPoint(*s, Point::active()).eql(Point::screen(0, 1)));
}

TEST(page_list, PageList_grow_allocate) {
    ListHolder s(opts(80, 24));

    /* Grow to capacity */
    Node *last_node = s->pages.last;
    Page *last = s->pages.last->page();
    const size_t n = (size_t)last->capacity.rows - last->size.rows;
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(growNode(&*s) == nullptr);
    }

    /* Grow, should allocate */
    Node *new_ = growNode(&*s);
    ASSERT_TRUE(new_ != nullptr);
    ASSERT_TRUE(s->pages.last == new_);
    ASSERT_TRUE(last_node->next == new_);
    {
        const PageList::Cell cell = s->getCell(Point::active(0, (uint32_t)(s->rows - 1))).value;
        ASSERT_TRUE(cell.node == new_);
        ASSERT_TRUE(Point::screen(0, last->capacity.rows).eql(cell.screenPoint()));
    }
}

TEST(page_list, PageList_Cell_screenPoint_supports_long_scrollback) {
    /* A modest number of full-size page nodes is enough to exceed the u16
     * row range without allocating any page backing memory. screenPoint only
     * reads the linked metadata while calculating the absolute coordinate. */
    const size_t page_count = 307;
    const size::CellCountInt rows_per_page = page::std_capacity().rows;
    Node *nodes = (Node *)calloc(page_count, sizeof(Node));

    for (size_t i = 0; i < page_count; i++) {
        Node *node = &nodes[i];
        node->prev = i > 0 ? &nodes[i - 1] : nullptr;
        node->next = i + 1 < page_count ? &nodes[i + 1] : nullptr;
        node->data.tag = Node::Data::Tag::resident;
        node->serial = i;
        node->owned = Node::Owned::heap;
        node->data.resident().size.cols = 1;
        node->data.resident().size.rows = rows_per_page;
    }

    const uint32_t expected_y = (uint32_t)(page_count - 1) * (uint32_t)rows_per_page;
    ASSERT_TRUE(expected_y > 0xFFFF);

    PageList::Cell cell;
    cell.node = &nodes[page_count - 1];
    cell.row = nullptr;
    cell.cell = nullptr;
    cell.row_idx = 0;
    cell.col_idx = 0;
    ASSERT_TRUE(Point::screen(0, expected_y).eql(cell.screenPoint()));
    free(nodes);
}

TEST(page_list, PageList_set_max_bytes_prunes_immediately_and_can_be_raised) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;
    const size_t item = PageList::PagePool::item_size;

    ListHolder s(opts(cols, 1));

    /* Build four complete pages of history followed by the active row. */
    ASSERT_TRUE(s->growRows(4 * page_rows));
    ASSERT_TRUE(5 == s->totalPages());

    Node *removed = s->pages.first;
    Node *retained = s->pages.last->prev;
    Pin *removed_pin = s->trackPin(Pin(removed));
    Pin *retained_pin = s->trackPin(Pin(retained));

    s->scroll(Scroll::pinAt(*retained_pin));
    ASSERT_TRUE(3 * page_rows == s->scrollbar().offset);

    /* The active-area minimum is two pages. Lowering below that immediately
     * removes all older complete historical pages. */
    s->setMaxBytes(item);
    ASSERT_TRUE(item == s->limits.bytes.explicit_);
    ASSERT_TRUE(2 * item == s->limits.max(PageList::Limits::Key::bytes));
    ASSERT_TRUE(s->limits.max(PageList::Limits::Key::bytes) == s->page_size);
    ASSERT_TRUE(2 == s->totalPages());
    ASSERT_TRUE(page_rows == s->total_rows - s->rows);
    ASSERT_TRUE(retained == s->pages.first);
    ASSERT_TRUE(retained == removed_pin->node);
    ASSERT_TRUE(removed_pin->garbage);
    ASSERT_TRUE(retained == retained_pin->node);
    ASSERT_FALSE(retained_pin->garbage);
    ASSERT_TRUE(0 == s->scrollbar().offset);

    /* Raising the limit doesn't allocate or otherwise change retained data,
     * but subsequent growth can exceed the previous effective limit. */
    const size_t limited_size = s->page_size;
    const size_t limited_rows = s->total_rows;
    s->setMaxBytes(8 * item);
    ASSERT_TRUE(limited_size == s->page_size);
    ASSERT_TRUE(limited_rows == s->total_rows);
    ASSERT_TRUE(s->growRows(2 * page_rows));
    ASSERT_TRUE(s->page_size > limited_size);

    /* Null restores unlimited growth and likewise preserves current data. */
    const size_t raised_size = s->page_size;
    const size_t raised_rows = s->total_rows;
    s->setMaxBytes(Maybe<size_t>());
    ASSERT_TRUE(SIZE_MAX == s->limits.bytes.explicit_);
    ASSERT_TRUE(raised_size == s->page_size);
    ASSERT_TRUE(raised_rows == s->total_rows);
    ASSERT_TRUE(s->growRows(5 * page_rows));
    ASSERT_TRUE(s->page_size > 8 * item);

    s->untrackPin(retained_pin);
    s->untrackPin(removed_pin);
}

TEST(page_list, PageList_set_max_bytes_zero_preserves_active_boundary) {
    ListHolder s(opts(80, 1));

    /* Make the sole page larger than the effective zero-byte limit. Its first
     * row will be history, but the same indivisible page also contains active. */
    while (s->page_size <= s->limits.bytes.min) {
        Node *n;
        ASSERT_TRUE(s->increaseCapacity(s->pages.first, PageList::IncreaseCapacity::grapheme_bytes, &n) ==
                    PageList::IncreaseCapacityError::none);
    }
    (void)growNode(&*s);
    ASSERT_TRUE(1 == s->totalPages());
    ASSERT_TRUE(s->pages.first == s->getTopLeft(point::Tag::active).node);
    ASSERT_TRUE(s->getTopLeft(point::Tag::active).y > 0);

    s->scroll(Scroll::top());
    ASSERT_TRUE(s->viewport == Viewport::top);

    s->setMaxBytes((size_t)0);
    ASSERT_TRUE(0 == s->limits.bytes.explicit_);
    ASSERT_TRUE(s->page_size > s->limits.max(PageList::Limits::Key::bytes));
    ASSERT_TRUE(1 == s->totalPages());
    ASSERT_TRUE(s->viewport == Viewport::active);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->rows, 0, s->rows));

    /* No-scrollback mode cannot be moved back into the retained boundary row. */
    s->scroll(Scroll::top());
    ASSERT_TRUE(s->viewport == Viewport::active);
}

typedef PageList::Limits Limits;
static const size_t kItem = PageList::PagePool::item_size;

/* Wisp: `.{ .cols = c, .rows = r, .reflow = f }` (negative = null). */
static PageList::Resize rz(int cols = -1, int rows = -1, bool reflow = true) {
    PageList::Resize r;
    if (cols >= 0) r.cols = (size::CellCountInt)cols;
    if (rows >= 0) r.rows = (size::CellCountInt)rows;
    r.reflow = reflow;
    return r;
}

static bool limitsEq(const Limits &a, const Limits &b) {
    return a.bytes.explicit_ == b.bytes.explicit_ && a.bytes.min == b.bytes.min &&
           a.lines.explicit_ == b.lines.explicit_ && a.lines.min == b.lines.min;
}

/* Wisp: grow `n` rows, tracking the most recently allocated page. */
static void growTracking(PageList &s, size_t n) {
    for (size_t i = 0; i < n; i++) (void)growNode(&s);
}

TEST(page_list, PageList_set_max_lines_prunes_immediately_and_can_be_raised) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;
    const size_t lowered_lines = page_rows + page_rows / 2;

    ListHolder s(opts(cols, 1));

    ASSERT_TRUE(s->growRows(4 * page_rows));
    ASSERT_TRUE(5 == s->totalPages());

    Node *removed = s->pages.first;
    Node *retained = s->pages.last->prev;
    Pin *removed_pin = s->trackPin(Pin(removed));
    Pin *retained_pin = s->trackPin(Pin(retained));

    s->scroll(Scroll::pinAt(*retained_pin));
    ASSERT_TRUE(3 * page_rows == s->scrollbar().offset);

    /* Whole-page enforcement undershoots a non-page-aligned line limit. */
    s->setMaxLines(lowered_lines);
    ASSERT_TRUE(lowered_lines == s->limits.lines.explicit_);
    ASSERT_TRUE(lowered_lines == s->limits.max(Limits::Key::lines));
    ASSERT_TRUE(page_rows == s->total_rows - s->rows);
    ASSERT_TRUE(2 == s->totalPages());
    ASSERT_TRUE(retained == s->pages.first);
    ASSERT_TRUE(retained == removed_pin->node);
    ASSERT_TRUE(removed_pin->garbage);
    ASSERT_TRUE(retained == retained_pin->node);
    ASSERT_FALSE(retained_pin->garbage);
    ASSERT_TRUE(0 == s->scrollbar().offset);

    const size_t limited_size = s->page_size;
    const size_t limited_rows = s->total_rows;
    s->setMaxLines(4 * page_rows);
    ASSERT_TRUE(limited_size == s->page_size);
    ASSERT_TRUE(limited_rows == s->total_rows);
    ASSERT_TRUE(s->growRows(2 * page_rows));
    ASSERT_TRUE(s->total_rows - s->rows > lowered_lines);

    const size_t raised_size = s->page_size;
    const size_t raised_rows = s->total_rows;
    s->setMaxLines(Maybe<size_t>());
    ASSERT_TRUE(SIZE_MAX == s->limits.lines.explicit_);
    ASSERT_TRUE(raised_size == s->page_size);
    ASSERT_TRUE(raised_rows == s->total_rows);
    ASSERT_TRUE(s->growRows(3 * page_rows));
    ASSERT_TRUE(s->total_rows - s->rows > 4 * page_rows);

    s->untrackPin(retained_pin);
    s->untrackPin(removed_pin);
}

TEST(page_list, PageList_set_max_limits_remain_independent) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;
    const size_t byte_limit = 3 * kItem;
    const size_t line_limit = page_rows / 2;

    ListHolder s(opts(cols, 1));

    ASSERT_TRUE(s->growRows(4 * page_rows));

    /* The byte setter leaves the line limit unlimited. */
    s->setMaxBytes(byte_limit);
    ASSERT_TRUE(byte_limit == s->limits.bytes.explicit_);
    ASSERT_TRUE(SIZE_MAX == s->limits.lines.explicit_);
    ASSERT_TRUE(3 == s->totalPages());
    ASSERT_TRUE(2 * page_rows == s->total_rows - s->rows);

    /* The smaller runtime line limit prunes one more complete page without
     * changing the configured byte limit. Its effective value is raised to
     * the existing one-page minimum. */
    s->setMaxLines(line_limit);
    ASSERT_TRUE(byte_limit == s->limits.bytes.explicit_);
    ASSERT_TRUE(line_limit == s->limits.lines.explicit_);
    ASSERT_TRUE(page_rows == s->limits.max(Limits::Key::lines));
    ASSERT_TRUE(2 == s->totalPages());
    ASSERT_TRUE(page_rows == s->total_rows - s->rows);

    /* Removing only the line limit leaves byte enforcement in effect. */
    s->setMaxLines(Maybe<size_t>());
    ASSERT_TRUE(s->growRows(3 * page_rows));
    ASSERT_TRUE(byte_limit == s->page_size);
    ASSERT_TRUE(3 == s->totalPages());
    ASSERT_TRUE(s->total_rows - s->rows > page_rows);
}

TEST(page_list, PageList_max_lines_uses_one_page_minimum) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;

    ListHolder s(opts(cols, 1, Maybe<size_t>(), page_rows / 2));

    ASSERT_TRUE(page_rows == s->limits.max(Limits::Key::lines));

    /* The requested limit is below one page, so a complete page of history
     * remains valid. */
    ASSERT_TRUE(s->growRows(page_rows));
    ASSERT_TRUE(page_rows == s->total_rows - s->rows);
    ASSERT_TRUE(2 == s->totalPages());

    Node *first = s->pages.first;
    const size_t old_page_size = s->page_size;

    /* One more row puts us over the effective limit. The now-complete
     * historical page is removed rather than partially trimmed. */
    (void)growNode(&*s);
    ASSERT_TRUE(1 == s->total_rows - s->rows);
    ASSERT_TRUE(1 == s->totalPages());
    ASSERT_TRUE(s->pages.first != first);
    ASSERT_TRUE(old_page_size - kItem == s->page_size);
}

TEST(page_list, PageList_max_lines_does_not_round_larger_limits) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;
    const size_t max_lines = page_rows + page_rows / 2;

    ListHolder s(opts(cols, 1, Maybe<size_t>(), max_lines));

    ASSERT_TRUE(max_lines == s->limits.max(Limits::Key::lines));
    ASSERT_TRUE(s->growRows(max_lines));
    ASSERT_TRUE(max_lines == s->total_rows - s->rows);

    Node *first = s->pages.first;
    Node *retained = first->next;
    Pin *removed_pin = s->trackPin(Pin(first));
    Pin *retained_pin = s->trackPin(Pin(retained));

    s->scroll(Scroll::pinAt(*retained_pin));
    ASSERT_TRUE(page_rows == s->scrollbar().offset);

    const size_t old_page_size = s->page_size;
    (void)growNode(&*s);

    /* Whole-page pruning undershoots the requested limit without rounding it. */
    ASSERT_TRUE(max_lines + 1 - page_rows == s->total_rows - s->rows);
    ASSERT_TRUE(retained == s->pages.first);
    ASSERT_TRUE(retained == removed_pin->node);
    ASSERT_TRUE(removed_pin->garbage);
    ASSERT_TRUE(retained == retained_pin->node);
    ASSERT_FALSE(retained_pin->garbage);
    ASSERT_TRUE(0 == s->scrollbar().offset);
    ASSERT_TRUE(old_page_size - kItem == s->page_size);

    s->untrackPin(retained_pin);
    s->untrackPin(removed_pin);
}

TEST(page_list, PageList_max_lines_and_max_size_enforce_the_smaller_limit) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;

    /* A line limit of one page keeps the logical allocation below a much
     * larger byte limit. */
    {
        ListHolder s(opts(cols, 1, 8 * kItem, page_rows));

        ASSERT_TRUE(s->growRows(4 * page_rows));
        ASSERT_TRUE(s->total_rows - s->rows <= s->limits.max(Limits::Key::lines));
        ASSERT_TRUE(s->totalPages() <= 2);
        ASSERT_TRUE(s->page_size < s->limits.max(Limits::Key::bytes));
    }

    /* A two-page byte limit prunes before the larger line limit is reached. */
    {
        ListHolder s(opts(cols, 1, kItem, 4 * page_rows));

        ASSERT_TRUE(s->growRows(2 * page_rows));
        ASSERT_TRUE(s->total_rows - s->rows < s->limits.max(Limits::Key::lines));
        ASSERT_TRUE(s->limits.max(Limits::Key::bytes) == s->page_size);
    }
}

TEST(page_list, PageList_max_lines_applies_to_resize_and_clone) {
    const size::CellCountInt cols = 80;
    const size_t page_rows = PageList::initialCapacity(cols).rows;

    ListHolder s(opts(cols, 2, Maybe<size_t>(), page_rows));

    ASSERT_TRUE(s->growRows(page_rows));
    ASSERT_TRUE(page_rows == s->total_rows - s->rows);

    /* Prevent row shrinking from trimming the trailing active row instead of
     * turning it into history. */
    *s->getCell(Point::active(0, 1)).value.cell = page::Cell::init('A');

    ASSERT_TRUE(s->resize(rz(-1, 1, false)));
    ASSERT_TRUE(1 == s->total_rows - s->rows);

    const size::CellCountInt new_cols = cols + 1;
    ASSERT_TRUE(s->resize(rz(new_cols, -1, true)));
    ASSERT_TRUE(Limits::minMaxLines(new_cols) == s->limits.lines.min);

    /* Exercise the same active-row shrink through the reflow path. Reflow
     * completes before the newly historical complete page is pruned. */
    {
        ListHolder reflowed(opts(cols, 2, Maybe<size_t>(), page_rows));

        ASSERT_TRUE(reflowed->growRows(page_rows));
        *reflowed->getCell(Point::active(0, 1)).value.cell = page::Cell::init('A');

        ASSERT_TRUE(reflowed->resize(rz(new_cols, 1, true)));
        ASSERT_TRUE(Limits::minMaxLines(new_cols) == reflowed->limits.lines.min);
        ASSERT_TRUE(reflowed->total_rows - reflowed->rows <= reflowed->limits.max(Limits::Key::lines) ||
                    reflowed->pages.first == reflowed->getTopLeft(point::Tag::active).node);
    }

    PageList cloned;
    ASSERT_TRUE(s->clone(talloc(), PageList::Clone(Point::screen()), &cloned) == page::PageError::none);

    ASSERT_TRUE(limitsEq(s->limits, cloned.limits));

    ASSERT_TRUE(cloned.growRows(2 * cloned.limits.max(Limits::Key::lines)));
    ASSERT_TRUE(cloned.total_rows - cloned.rows <= cloned.limits.max(Limits::Key::lines) ||
                cloned.pages.first == cloned.getTopLeft(point::Tag::active).node);
    cloned.deinit();
}

TEST(page_list, PageList_grow_prune_scrollback) {
    /* Use std_size to limit scrollback so pruning is triggered. */
    ListHolder s(opts(80, 24, PageList::std_size));

    /* Grow to capacity */
    Node *page1_node = s->pages.last;
    Page *page1 = page1_node->page();
    {
        const size_t n = (size_t)page1->capacity.rows - page1->size.rows;
        for (size_t i = 0; i < n; i++) ASSERT_TRUE(growNode(&*s) == nullptr);
    }

    /* Grow and allocate one more page. Then fill that page up. */
    Node *page2_node = growNode(&*s);
    ASSERT_TRUE(page2_node != nullptr);
    Page *page2 = page2_node->page();
    {
        const size_t n = (size_t)page2->capacity.rows - page2->size.rows;
        for (size_t i = 0; i < n; i++) ASSERT_TRUE(growNode(&*s) == nullptr);
    }

    /* Get our page size */
    const size_t old_page_size = s->page_size;

    /* Create a tracked pin in the first page */
    Pin *p = s->trackPin(s->pin(Point::screen()).value);
    ASSERT_TRUE(p->node == s->pages.first);

    /* Scroll back to create a pinned viewport (not active) */
    const size_t pin_y = page1->capacity.rows / 2;
    s->scroll(Scroll::pinAt(s->pin(Point::screen(0, (uint32_t)pin_y)).value));
    ASSERT_TRUE(s->viewport == Viewport::pin);

    /* Get the scrollbar state to populate the cache */
    const Scrollbar scrollbar_before = s->scrollbar();
    ASSERT_TRUE(pin_y == scrollbar_before.offset);

    /* Next should create a new page, but it should reuse our first
     * page since we're at max size. */
    Node *new_ = growNode(&*s);
    ASSERT_TRUE(new_ != nullptr);
    ASSERT_TRUE(s->pages.last == new_);
    ASSERT_TRUE(s->page_size == old_page_size);

    /* Our first should now be page2 and our last should be page1 */
    ASSERT_TRUE(page2_node == s->pages.first);
    ASSERT_TRUE(page1_node == s->pages.last);

    /* Our tracked pin should point to the top-left of the first page */
    ASSERT_TRUE(p->node == s->pages.first);
    ASSERT_TRUE(p->x == 0);
    ASSERT_TRUE(p->y == 0);
    ASSERT_TRUE(p->garbage);

    /* Verify the viewport offset cache was invalidated. After pruning,
     * the offset should have changed because we removed rows from
     * the beginning. */
    {
        const Scrollbar scrollbar_after = s->scrollbar();
        const size_t rows_pruned = page1->capacity.rows;
        const size_t expected_offset = pin_y >= rows_pruned ? pin_y - rows_pruned : 0;
        ASSERT_TRUE(expected_offset == scrollbar_after.offset);
    }
    s->untrackPin(p);
}

TEST(page_list, PageList_grow_prune_scrollback_with_viewport_pin_not_in_pruned_page) {
    /* Use std_size to limit scrollback so pruning is triggered. */
    ListHolder s(opts(80, 24, PageList::std_size));

    /* Grow to capacity of first page */
    Node *page1_node = s->pages.last;
    Page *page1 = page1_node->page();
    {
        const size_t n = (size_t)page1->capacity.rows - page1->size.rows;
        for (size_t i = 0; i < n; i++) ASSERT_TRUE(growNode(&*s) == nullptr);
    }

    /* Grow and allocate second page, then fill it up */
    Node *page2_node = growNode(&*s);
    ASSERT_TRUE(page2_node != nullptr);
    Page *page2 = page2_node->page();
    {
        const size_t n = (size_t)page2->capacity.rows - page2->size.rows;
        for (size_t i = 0; i < n; i++) ASSERT_TRUE(growNode(&*s) == nullptr);
    }

    /* Get our page size */
    const size_t old_page_size = s->page_size;

    /* Scroll back to create a pinned viewport in page2 (NOT page1)
     * This is the key difference from the previous test - the viewport
     * pin is NOT in the page that will be pruned. */
    const size_t pin_y = (size_t)page1->capacity.rows + 5;
    s->scroll(Scroll::pinAt(s->pin(Point::screen(0, (uint32_t)pin_y)).value));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(s->viewport_pin->node == page2_node);

    /* Get the scrollbar state to populate the cache */
    const Scrollbar scrollbar_before = s->scrollbar();
    ASSERT_TRUE(pin_y == scrollbar_before.offset);

    /* Next grow will trigger pruning of the first page.
     * The viewport_pin.node is page2, not page1, so it won't be moved
     * by the pin update loop, but the cached offset still needs to be
     * invalidated because rows were removed from the beginning. */
    Node *new_ = growNode(&*s);
    ASSERT_TRUE(new_ != nullptr);
    ASSERT_TRUE(s->pages.last == new_);
    ASSERT_TRUE(s->page_size == old_page_size);

    /* Our first should now be page2 (page1 was pruned) */
    ASSERT_TRUE(page2_node == s->pages.first);

    /* The viewport pin should still be on page2, unchanged */
    ASSERT_TRUE(s->viewport_pin->node == page2_node);

    /* Verify the viewport offset cache was invalidated/updated.
     * After pruning, the offset should have decreased by the number
     * of rows that were pruned. */
    const Scrollbar scrollbar_after = s->scrollbar();
    const size_t rows_pruned = page1->capacity.rows;
    const size_t expected_offset = pin_y - rows_pruned;
    ASSERT_TRUE(expected_offset == scrollbar_after.offset);
}

TEST(page_list, PageList_eraseRows_invalidates_viewport_offset_cache) {
    ListHolder s(opts(80, 24));

    /* Grow so we take up several pages worth of history */
    Page *page = s->pages.last->page();
    const size_t cap_rows = page->capacity.rows;
    growTracking(*s, cap_rows * 3);

    /* Scroll back to create a pinned viewport somewhere in the middle
     * of the scrollback */
    const size_t pin_y = cap_rows;
    s->scroll(Scroll::pinAt(s->pin(Point::screen(0, (uint32_t)pin_y)).value));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y, s->rows));

    /* Erase some history rows BEFORE the viewport pin.
     * This removes rows from before our pin, which changes its absolute
     * offset from the top, but the cache is not invalidated. */
    const size_t rows_to_erase = cap_rows / 2;
    s->eraseHistory(Point::history(0, (uint32_t)(rows_to_erase - 1)));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y - rows_to_erase, s->rows));
}

TEST(page_list, PageList_eraseRow_invalidates_viewport_offset_cache) {
    ListHolder s(opts(80, 24));

    /* Grow so we take up several pages worth of history */
    Page *page = s->pages.last->page();
    const size_t cap_rows = page->capacity.rows;
    growTracking(*s, cap_rows * 3);

    /* Scroll back to create a pinned viewport somewhere in the middle
     * of the scrollback */
    const size_t pin_y = cap_rows;
    s->scroll(Scroll::pinAt(s->pin(Point::screen(0, (uint32_t)pin_y)).value));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y, s->rows));

    /* Erase a single row from the history BEFORE the viewport pin.
     * This removes one row from before our pin, which changes its absolute
     * offset from the top by 1, but the cache is not invalidated. */
    s->eraseRow(Point::history(0, 0));

    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y - 1, s->rows));
}

TEST(page_list, PageList_eraseRowBounded_invalidates_viewport_offset_cache) {
    ListHolder s(opts(80, 24));

    /* Grow so we take up several pages worth of history */
    Page *page = s->pages.last->page();
    growTracking(*s, (size_t)page->capacity.rows * 3);

    /* Scroll back to create a pinned viewport somewhere in the middle
     * of the scrollback */
    const uint16_t pin_y = 4;
    s->scroll(Scroll::pinAt(s->pin(Point::screen(0, pin_y)).value));
    ASSERT_TRUE(s->viewport == Viewport::pin);
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y, s->rows));

    /* Erase a row from the history BEFORE the viewport pin with a bounded
     * shift. This removes one row from before our pin, which changes its
     * absolute offset from the top by 1, but the cache is not invalidated. */
    s->eraseRowBounded(Point::history(0, 0), 10);

    /* Verify the scrollbar reflects the change (offset decreased by 1) */
    ASSERT_TRUE(sbEq(s->scrollbar(), s->total_rows, pin_y - 1, s->rows));
}

/* @@TESTS@@ */

/* Wisp: std.testing.allocator's leak check. Runs last (registration order). */
TEST(page_list, zz_Wisp_no_leaks) { ASSERT_TRUE(zigstd::testing_state().live == 0); }
