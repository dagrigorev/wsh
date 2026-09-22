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

/* @@TESTS@@ */

/* Wisp: std.testing.allocator's leak check. Runs last (registration order). */
TEST(page_list, zz_Wisp_no_leaks) { ASSERT_TRUE(zigstd::testing_state().live == 0); }
