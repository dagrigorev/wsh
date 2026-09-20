/* Tests for src/terminal/page_list.hpp.
 *
 * Related to Ghostty src/terminal/PageList.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * The list is where a page running out stops being a problem. These tests are
 * about the seams: a pin walking off one page onto the next, a row index
 * landing in the right page, and the front of the list being freed when the
 * scrollback limit is reached.
 */

#include "test_helpers.h"
#include "page_list.hpp"

using namespace wisp::terminal;

/* A list whose pages are deliberately small, so a handful of rows crosses
 * several of them and the seams are reachable from a test. Real pages hold
 * hundreds of rows, which no test wants to write out. */
static bool small_list(PageList *l, CellCountInt cols, CellCountInt rows,
                       CellCountInt page_rows, size_t pages) {
    *l = PageList();
    l->cols = cols;
    l->rows = rows;
    for (size_t i = 0; i < pages; i++) {
        if (!page_list_append(l, page_rows)) return false;
    }
    return true;
}

/* ─── setting up ─────────────────────────────────────────────────────────── */

TEST(page_list, init_gives_one_page_holding_the_screen) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    ASSERT_EQ(l.page_count, 1u);
    ASSERT_TRUE(l.first == l.last);
    ASSERT_TRUE(l.row_count >= 24u);
    ASSERT_EQ(l.first->page.capacity.cols, 80);

    page_list_deinit(&l);
    ASSERT_TRUE(l.first == nullptr);
    ASSERT_EQ(l.page_count, 0u);
}

TEST(page_list, a_page_is_near_the_target_size) {
    /* The row count is derived from the layout rather than from dividing,
     * because a page's overhead does not shrink with its rows. */
    const CellCountInt rows = page_list_rows_per_page(80, 24);
    ASSERT_TRUE(rows > 24);

    ASSERT_TRUE(PageLayout::init(Capacity(80, rows)).total_size <=
                PAGE_TARGET_BYTES);
    ASSERT_TRUE(PageLayout::init(Capacity(80, (CellCountInt)(rows + 1)))
                    .total_size > PAGE_TARGET_BYTES);
}

TEST(page_list, a_wide_screen_still_gets_its_rows) {
    /* At some width a page of the target size cannot hold a screenful. The
     * screen wins: a page that cannot show what the user is looking at would
     * be useless however tidy its size. */
    const CellCountInt rows = page_list_rows_per_page(1000, 200);
    ASSERT_TRUE(rows >= 200);
}

/* ─── growing ────────────────────────────────────────────────────────────── */

TEST(page_list, appending_links_both_ways) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));
    PageNode *first = l.first;

    PageNode *second = page_list_append(&l, 5);
    ASSERT_TRUE(second != nullptr);

    ASSERT_EQ(l.page_count, 2u);
    ASSERT_TRUE(l.last == second);
    ASSERT_TRUE(first->next == second);
    ASSERT_TRUE(second->prev == first);
    ASSERT_TRUE(second->next == nullptr);

    page_list_deinit(&l);
}

TEST(page_list, row_count_follows_the_pages) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 1));
    ASSERT_EQ(l.row_count, 5u);

    ASSERT_TRUE(page_list_append(&l, 5) != nullptr);
    ASSERT_EQ(l.row_count, 10u);

    page_list_drop_first(&l);
    ASSERT_EQ(l.row_count, 5u);

    page_list_deinit(&l);
}

/* ─── addressing across pages ────────────────────────────────────────────── */

TEST(page_list, pin_lands_in_the_right_page) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    Pin a = page_list_pin(&l, 0);
    ASSERT_TRUE(a.node == l.first);
    ASSERT_EQ(a.y, 0);

    Pin b = page_list_pin(&l, 4);
    ASSERT_TRUE(b.node == l.first);
    ASSERT_EQ(b.y, 4);

    /* The seam: row 5 is the first row of the second page. */
    Pin c = page_list_pin(&l, 5);
    ASSERT_TRUE(c.node == l.first->next);
    ASSERT_EQ(c.y, 0);

    Pin d = page_list_pin(&l, 14);
    ASSERT_TRUE(d.node == l.last);
    ASSERT_EQ(d.y, 4);

    page_list_deinit(&l);
}

TEST(page_list, pin_past_the_end_is_invalid) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    ASSERT_FALSE(page_list_pin(&l, 10).valid());
    page_list_deinit(&l);
}

TEST(page_list, pin_reaches_the_cell_it_names) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    Pin p = page_list_pin(&l, 7);
    p.x = 3;
    p.cell()->set_codepoint('k');

    ASSERT_EQ(l.first->next->page.get_cell(3, 2)->codepoint(), 'k');
    page_list_deinit(&l);
}

/* ─── walking ────────────────────────────────────────────────────────────── */

TEST(page_list, pin_walks_down_across_a_seam) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    Pin p = page_list_pin(&l, 0);
    ASSERT_TRUE(p.down(4));
    ASSERT_TRUE(p.node == l.first);
    ASSERT_EQ(p.y, 4);

    /* One more row is the next page, which the caller never has to notice. */
    ASSERT_TRUE(p.down(1));
    ASSERT_TRUE(p.node == l.first->next);
    ASSERT_EQ(p.y, 0);

    page_list_deinit(&l);
}

TEST(page_list, pin_walks_down_across_several_pages) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    Pin p = page_list_pin(&l, 0);
    ASSERT_TRUE(p.down(12));
    ASSERT_TRUE(p.node == l.last);
    ASSERT_EQ(p.y, 2);

    page_list_deinit(&l);
}

TEST(page_list, pin_walks_up_across_a_seam) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    Pin p = page_list_pin(&l, 10);   /* first row of the third page */
    ASSERT_TRUE(p.up(1));
    ASSERT_TRUE(p.node == l.first->next);
    ASSERT_EQ(p.y, 4);

    ASSERT_TRUE(p.up(9));
    ASSERT_TRUE(p.node == l.first);
    ASSERT_EQ(p.y, 0);

    page_list_deinit(&l);
}

TEST(page_list, walking_past_either_end_fails_and_changes_nothing) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    Pin top = page_list_pin(&l, 0);
    Pin saved = top;
    ASSERT_FALSE(top.up(1));
    ASSERT_TRUE(top == saved);

    Pin bottom = page_list_pin(&l, 9);
    saved = bottom;
    ASSERT_FALSE(bottom.down(1));
    ASSERT_TRUE(bottom == saved);

    page_list_deinit(&l);
}

TEST(page_list, walking_by_zero_stays_put) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    Pin p = page_list_pin(&l, 6);
    ASSERT_TRUE(p.down(0));
    ASSERT_TRUE(p.up(0));
    ASSERT_TRUE(p.node == l.first->next);
    ASSERT_EQ(p.y, 1);

    page_list_deinit(&l);
}

TEST(page_list, walking_down_then_up_returns) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 4));

    Pin p = page_list_pin(&l, 2);
    const Pin start = p;
    ASSERT_TRUE(p.down(11));
    ASSERT_TRUE(p.up(11));
    ASSERT_TRUE(p == start);

    page_list_deinit(&l);
}

/* ─── the screen ─────────────────────────────────────────────────────────── */

TEST(page_list, active_is_the_last_rows_of_the_list) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));   /* 15 rows, screen shows 4 */

    Pin start = page_list_active_start(&l);
    ASSERT_TRUE(start.node == l.last);
    ASSERT_EQ(start.y, 1);

    /* Screen row 3 is the last row of the list. */
    Pin last = page_list_active_pin(&l, 0, 3);
    ASSERT_TRUE(last.node == l.last);
    ASSERT_EQ(last.y, 4);

    page_list_deinit(&l);
}

TEST(page_list, active_starts_at_the_top_when_the_list_is_short) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 24, 5, 1));

    /* Fewer rows than the screen shows: it starts at the beginning rather
     * than at a negative row. */
    Pin start = page_list_active_start(&l);
    ASSERT_TRUE(start.node == l.first);
    ASSERT_EQ(start.y, 0);

    page_list_deinit(&l);
}

TEST(page_list, active_can_span_a_seam) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 8, 5, 2));   /* 10 rows, screen shows 8 */

    Pin start = page_list_active_start(&l);
    ASSERT_TRUE(start.node == l.first);
    ASSERT_EQ(start.y, 2);

    Pin mid = page_list_active_pin(&l, 0, 3);
    ASSERT_TRUE(mid.node == l.last);
    ASSERT_EQ(mid.y, 0);

    page_list_deinit(&l);
}

/* ─── forgetting ─────────────────────────────────────────────────────────── */

TEST(page_list, trimming_frees_the_oldest_pages) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 6));

    /* Forgetting scrollback is not compaction: the rows are not moved
     * anywhere, the page holding them is freed. */
    const size_t page_bytes = l.first->page.size;
    l.max_size = page_bytes * 3;
    page_list_trim(&l);

    ASSERT_EQ(l.page_count, 3u);
    ASSERT_TRUE(l.bytes <= l.max_size);
    ASSERT_TRUE(l.first->prev == nullptr);

    page_list_deinit(&l);
}

TEST(page_list, trimming_never_gives_up_the_screen) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 12, 5, 6));

    /* A limit of one page cannot be met without losing rows the user is
     * looking at, so it is honored as far as it can be and no further. */
    l.max_size = l.first->page.size;
    page_list_trim(&l);

    ASSERT_TRUE(l.row_count >= 12u);
    ASSERT_TRUE(l.bytes > l.max_size);

    page_list_deinit(&l);
}

TEST(page_list, trimming_keeps_the_last_page) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 1, 5, 2));

    l.max_size = 1;
    page_list_trim(&l);

    ASSERT_EQ(l.page_count, 1u);
    ASSERT_TRUE(l.first == l.last);

    page_list_deinit(&l);
}

TEST(page_list, no_limit_means_no_trimming) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 5));

    l.max_size = 0;
    page_list_trim(&l);
    ASSERT_EQ(l.page_count, 5u);

    page_list_deinit(&l);
}

TEST(page_list, growing_past_the_limit_trims_as_it_goes) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));
    l.max_size = l.first->page.size * 3;

    /* Ten pages worth of rows against a three-page limit. This is the steady
     * state a long session settles into: pages are recycled off the front as
     * fast as they are filled at the back. */
    const size_t per_page = (size_t)page_list_rows_per_page(80, 24);
    ASSERT_EQ(page_list_grow_rows(&l, per_page * 10), per_page * 10);

    ASSERT_EQ(l.page_count, 3u);
    ASSERT_TRUE(l.bytes <= l.max_size);

    page_list_deinit(&l);
}

/* ─── what pins survive ──────────────────────────────────────────────────── */

TEST(page_list, a_pin_outlives_rows_being_renumbered) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    Pin p = page_list_pin(&l, 7);
    p.x = 1;
    p.cell()->set_codepoint('p');

    /* Adding pages renumbers every row counted from the start of the list,
     * and the pin does not care: it names a page, and a page's rows do not
     * move while it lives. */
    ASSERT_TRUE(page_list_append(&l, 5) != nullptr);
    ASSERT_TRUE(page_list_append(&l, 5) != nullptr);

    ASSERT_EQ(p.cell()->codepoint(), 'p');
    ASSERT_EQ(page_list_pin(&l, 7).node, p.node);

    page_list_deinit(&l);
}

TEST(page_list, scrolling_a_page_does_not_disturb_the_list) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 5, 5, 2));

    Page *page = &l.last->page;
    for (CellCountInt y = 0; y < 5; y++) {
        page->get_cell(0, y)->set_codepoint((uint32_t)('A' + y));
    }

    page_scroll_up(page, 0, 4, 2);

    ASSERT_EQ(l.row_count, 10u);
    ASSERT_EQ(page->get_cell(0, 0)->codepoint(), 'C');
    ASSERT_EQ(page->get_cell(0, 3)->codepoint(), 0u);

    page_list_deinit(&l);
}

/* ─── the viewport ───────────────────────────────────────────────────────── */

TEST(viewport, starts_following_the_active_area) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    ASSERT_TRUE(l.viewport == ViewportTag::active);
    ASSERT_TRUE(page_list_viewport_start(&l) == page_list_active_start(&l));
    ASSERT_EQ(page_list_viewport_offset(&l), 0u);

    page_list_deinit(&l);
}

TEST(viewport, following_the_active_area_needs_no_correction) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    /* The viewport is an intention, not a row. Growing the list moves what
     * the active area *is*, and a viewport following it comes along without
     * anything being recomputed. */
    ASSERT_TRUE(page_list_viewport_start(&l).node == l.last);
    ASSERT_TRUE(page_list_append(&l, 5) != nullptr);

    ASSERT_TRUE(page_list_viewport_start(&l).node == l.last);
    ASSERT_EQ(page_list_viewport_offset(&l), 0u);

    page_list_deinit(&l);
}

TEST(viewport, a_pinned_viewport_stays_put_as_output_arrives) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 2));

    Pin p = page_list_pin(&l, 3);
    page_list_scroll_to_pin(&l, p);
    ASSERT_TRUE(l.viewport == ViewportTag::pin);

    /* This is a user who scrolled up by hand. New output must arrive below
     * them, not drag the screen along. */
    ASSERT_TRUE(page_list_append(&l, 5) != nullptr);

    ASSERT_TRUE(page_list_viewport_start(&l) == p);
    ASSERT_EQ(page_list_viewport_offset(&l), 8u);

    page_list_deinit(&l);
}

TEST(viewport, scrolling_up_then_down_returns_to_the_active_area) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));   /* 15 rows, 11 of scrollback */

    page_list_scroll_delta(&l, -5);
    ASSERT_TRUE(l.viewport == ViewportTag::pin);
    ASSERT_EQ(page_list_viewport_offset(&l), 5u);

    page_list_scroll_delta(&l, 5);
    ASSERT_EQ(page_list_viewport_offset(&l), 0u);

    /* Back at the bottom it follows the active area again rather than
     * pinning the row that happens to be there — scroll down once and new
     * output keeps you there. */
    ASSERT_TRUE(l.viewport == ViewportTag::active);

    page_list_deinit(&l);
}

TEST(viewport, scrolling_up_past_the_start_clamps_to_the_top) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    page_list_scroll_delta(&l, -9999);
    ASSERT_TRUE(l.viewport == ViewportTag::top);
    ASSERT_TRUE(page_list_viewport_start(&l) == page_list_pin(&l, 0));
    ASSERT_EQ(page_list_viewport_offset(&l), page_list_max_scroll(&l));

    page_list_deinit(&l);
}

TEST(viewport, scrolling_down_past_the_end_clamps_to_the_active_area) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    page_list_scroll_top(&l);
    page_list_scroll_delta(&l, 9999);
    ASSERT_TRUE(l.viewport == ViewportTag::active);

    page_list_deinit(&l);
}

TEST(viewport, scrolling_by_zero_changes_nothing) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    page_list_scroll_delta(&l, -4);
    const Pin before = page_list_viewport_start(&l);
    page_list_scroll_delta(&l, 0);

    ASSERT_TRUE(page_list_viewport_start(&l) == before);
    page_list_deinit(&l);
}

TEST(viewport, a_short_list_has_nowhere_to_scroll) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 24, 5, 1));

    /* Fewer rows than the screen shows, so there is no scrollback and every
     * scroll lands back at the active area. */
    ASSERT_EQ(page_list_max_scroll(&l), 0u);
    page_list_scroll_delta(&l, -10);
    ASSERT_TRUE(l.viewport == ViewportTag::active);

    page_list_deinit(&l);
}

TEST(viewport, top_follows_the_oldest_surviving_row) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 4));

    page_list_scroll_top(&l);
    ASSERT_TRUE(page_list_viewport_start(&l).node == l.first);

    /* Like active, top is an intention: trimming the oldest page moves what
     * "the top" means, and the viewport follows without being told. */
    page_list_drop_first(&l);
    ASSERT_TRUE(page_list_viewport_start(&l).node == l.first);

    page_list_deinit(&l);
}

TEST(viewport, a_pin_in_a_trimmed_page_falls_back_to_the_top) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 4));

    page_list_scroll_to_pin(&l, page_list_pin(&l, 2));
    ASSERT_TRUE(l.viewport_pin.node == l.first);

    /* The page the viewport was pinned into is freed. Left alone the pin
     * would point at memory that is gone, so it falls back to the nearest
     * thing that still exists. */
    page_list_drop_first(&l);

    ASSERT_TRUE(l.viewport == ViewportTag::top);
    ASSERT_TRUE(page_list_viewport_start(&l).node == l.first);

    page_list_deinit(&l);
}

TEST(viewport, a_pin_in_a_surviving_page_is_untouched_by_trimming) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 4));

    Pin p = page_list_pin(&l, 12);
    page_list_scroll_to_pin(&l, p);
    page_list_drop_first(&l);

    ASSERT_TRUE(l.viewport == ViewportTag::pin);
    ASSERT_TRUE(page_list_viewport_start(&l) == p);

    page_list_deinit(&l);
}

TEST(viewport, growing_past_the_limit_recycles_under_a_pinned_viewport) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));
    l.max_size = l.first->page.size * 3;

    page_list_scroll_to_pin(&l, page_list_pin(&l, 12));

    /* Enough growth that every page present now is eventually freed. The
     * viewport must end up somewhere valid rather than dangling. */
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(page_list_append(&l, 5) != nullptr);
        page_list_trim(&l);
    }

    Pin v = page_list_viewport_start(&l);
    ASSERT_TRUE(v.valid());
    ASSERT_TRUE(page_list_row_index(&l, v) != (size_t)-1);

    page_list_deinit(&l);
}

TEST(viewport, reads_the_rows_it_points_at) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));

    for (size_t i = 0; i < 15; i++) {
        Pin p = page_list_pin(&l, i);
        p.x = 0;
        p.cell()->set_codepoint((uint32_t)('a' + i));
    }

    page_list_scroll_delta(&l, -11);   /* all the way up */
    ASSERT_EQ(page_list_viewport_cell(&l, 0, 0).cell()->codepoint(), 'a');
    ASSERT_EQ(page_list_viewport_cell(&l, 0, 3).cell()->codepoint(), 'd');

    page_list_scroll_active(&l);
    ASSERT_EQ(page_list_viewport_cell(&l, 0, 0).cell()->codepoint(), 'l');
    ASSERT_EQ(page_list_viewport_cell(&l, 0, 3).cell()->codepoint(), 'o');

    page_list_deinit(&l);
}

TEST(viewport, row_index_finds_a_pin_and_rejects_a_stranger) {
    PageList l, other;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));
    ASSERT_TRUE(small_list(&other, 10, 4, 5, 1));

    ASSERT_EQ(page_list_row_index(&l, page_list_pin(&l, 7)), 7u);
    ASSERT_EQ(page_list_row_index(&l, page_list_pin(&other, 0)), (size_t)-1);
    ASSERT_EQ(page_list_row_index(&l, Pin()), (size_t)-1);

    page_list_deinit(&other);
    page_list_deinit(&l);
}

/* ─── scrolling the screen ───────────────────────────────────────────────── */

/* A newline at the bottom of the screen is one grow. Everything about
 * scrollback follows from that: the active area is the last `rows` rows, so
 * adding a row at the end pushes the oldest one out of it without moving
 * anything. */

TEST(scrolling, a_fresh_list_has_no_scrollback) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    /* A page holds hundreds of rows, but a terminal that has printed nothing
     * has no scrollback however large the page it was handed. */
    ASSERT_EQ(l.row_count, 24u);
    ASSERT_EQ(page_list_max_scroll(&l), 0u);
    ASSERT_TRUE(l.first->page.capacity.rows > l.first->rows_used);

    page_list_deinit(&l);
}

TEST(scrolling, growing_adds_one_row_not_one_page) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    Pin p = page_list_grow(&l);
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(l.row_count, 25u);
    ASSERT_EQ(l.page_count, 1u);
    ASSERT_TRUE(p.node == l.first);
    ASSERT_EQ(p.y, 24);

    page_list_deinit(&l);
}

TEST(scrolling, the_oldest_row_becomes_scrollback) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    Pin top = page_list_active_start(&l);
    ASSERT_EQ(page_list_row_index(&l, top), 0u);

    page_list_grow(&l);

    /* Row 0 did not move. The active area did. */
    ASSERT_EQ(page_list_row_index(&l, page_list_active_start(&l)), 1u);
    ASSERT_EQ(page_list_max_scroll(&l), 1u);
    ASSERT_TRUE(page_list_pin(&l, 0) == top);

    page_list_deinit(&l);
}

TEST(scrolling, text_stays_where_it_was_written) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    Pin first = page_list_pin(&l, 0);
    first.x = 0;
    first.cell()->set_codepoint('q');

    ASSERT_EQ(page_list_grow_rows(&l, 100), 100u);

    /* A hundred lines later the pin still reads what was written through it.
     * Scrolling a terminal copies nothing. */
    ASSERT_EQ(first.cell()->codepoint(), 'q');
    ASSERT_EQ(page_list_pin(&l, 0).node, first.node);

    page_list_deinit(&l);
}

TEST(scrolling, a_full_page_hands_over_to_a_new_one) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 1));
    l.first->rows_used = 4;
    l.row_count = 4;

    ASSERT_TRUE(page_list_grow(&l).valid());
    ASSERT_EQ(l.page_count, 1u);   /* the fifth row fits */

    /* The sixth does not, so a new page takes over and the old one keeps
     * every row it already held. */
    Pin p = page_list_grow(&l);
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(l.page_count, 2u);
    ASSERT_TRUE(p.node == l.last);
    ASSERT_EQ(p.y, 0);
    ASSERT_EQ(l.first->rows_used, 5);
    ASSERT_EQ(l.last->rows_used, 1);
    ASSERT_EQ(l.row_count, 6u);

    page_list_deinit(&l);
}

TEST(scrolling, rows_are_continuous_across_the_handover) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 2, 3, 1));
    l.first->rows_used = 2;
    l.row_count = 2;

    /* Write a running count through the pins grow hands back, then read it
     * off by index. A seam that lost or duplicated a row shows up here. */
    for (size_t i = 0; i < 2; i++) {
        Pin p = page_list_pin(&l, i);
        p.x = 0;
        p.cell()->set_codepoint((uint32_t)('0' + i));
    }
    for (size_t i = 2; i < 12; i++) {
        Pin p = page_list_grow(&l);
        ASSERT_TRUE(p.valid());
        p.x = 0;
        p.cell()->set_codepoint((uint32_t)('0' + i));
    }

    ASSERT_EQ(l.row_count, 12u);
    for (size_t i = 0; i < 12; i++) {
        Pin p = page_list_pin(&l, i);
        p.x = 0;
        ASSERT_EQ(p.cell()->codepoint(), (uint32_t)('0' + i));
    }

    page_list_deinit(&l);
}

TEST(scrolling, a_new_row_arrives_blank) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 2, 5, 1));
    l.first->rows_used = 2;
    l.row_count = 2;

    /* Dirty the row grow is about to hand out, the way a page reused after
     * trimming would be. */
    l.first->page.get_cell(0, 2)->set_codepoint('x');
    l.first->page.get_row(2)->set_wrap(true);

    Pin p = page_list_grow(&l);
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(p.cell()->codepoint(), 0u);
    ASSERT_FALSE(p.row()->wrap());

    page_list_deinit(&l);
}

TEST(scrolling, an_active_viewport_follows_the_output) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));

    ASSERT_EQ(page_list_grow_rows(&l, 50), 50u);

    /* The user has not scrolled, so they are still looking at the newest
     * output — and nothing had to be told about it. */
    ASSERT_TRUE(l.viewport == ViewportTag::active);
    ASSERT_EQ(page_list_viewport_offset(&l), 0u);
    ASSERT_EQ(page_list_row_index(&l, page_list_viewport_start(&l)), 50u);

    page_list_deinit(&l);
}

TEST(scrolling, a_pinned_viewport_drifts_away_from_the_output) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));
    ASSERT_EQ(page_list_grow_rows(&l, 50), 50u);

    page_list_scroll_delta(&l, -10);
    ASSERT_EQ(page_list_viewport_offset(&l), 10u);

    /* More output arrives while the user is reading something older. The
     * screen must not move under them, so the distance grows. */
    ASSERT_EQ(page_list_grow_rows(&l, 5), 5u);
    ASSERT_EQ(page_list_viewport_offset(&l), 15u);

    page_list_scroll_active(&l);
    ASSERT_EQ(page_list_viewport_offset(&l), 0u);

    page_list_deinit(&l);
}

TEST(scrolling, scrollback_is_bounded_by_the_limit) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));
    l.max_size = l.first->page.size * 2;

    const size_t per_page = (size_t)page_list_rows_per_page(80, 24);
    ASSERT_EQ(page_list_grow_rows(&l, per_page * 6), per_page * 6);

    /* Old rows are forgotten, but never so many that the screen goes with
     * them, and the row count stays consistent with the pages. */
    ASSERT_TRUE(l.row_count >= 24u);
    ASSERT_TRUE(l.bytes <= l.max_size);

    size_t counted = 0;
    for (PageNode *n = l.first; n; n = n->next) counted += n->rows_used;
    ASSERT_EQ(counted, l.row_count);

    page_list_deinit(&l);
}
