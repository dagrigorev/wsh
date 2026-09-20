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

TEST(page_list, grow_appends_and_links_both_ways) {
    PageList l;
    ASSERT_TRUE(page_list_init(&l, 80, 24, 0));
    PageNode *first = l.first;

    PageNode *second = page_list_grow(&l);
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

TEST(page_list, screen_is_the_last_rows_of_the_list) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 4, 5, 3));   /* 15 rows, screen shows 4 */

    Pin start = page_list_screen_start(&l);
    ASSERT_TRUE(start.node == l.last);
    ASSERT_EQ(start.y, 1);

    /* Screen row 3 is the last row of the list. */
    Pin last = page_list_screen_pin(&l, 0, 3);
    ASSERT_TRUE(last.node == l.last);
    ASSERT_EQ(last.y, 4);

    page_list_deinit(&l);
}

TEST(page_list, screen_starts_at_the_top_when_the_list_is_short) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 24, 5, 1));

    /* Fewer rows than the screen shows: it starts at the beginning rather
     * than at a negative row. */
    Pin start = page_list_screen_start(&l);
    ASSERT_TRUE(start.node == l.first);
    ASSERT_EQ(start.y, 0);

    page_list_deinit(&l);
}

TEST(page_list, screen_can_span_a_seam) {
    PageList l;
    ASSERT_TRUE(small_list(&l, 10, 8, 5, 2));   /* 10 rows, screen shows 8 */

    Pin start = page_list_screen_start(&l);
    ASSERT_TRUE(start.node == l.first);
    ASSERT_EQ(start.y, 2);

    Pin mid = page_list_screen_pin(&l, 0, 3);
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

    for (int i = 0; i < 20; i++) ASSERT_TRUE(page_list_grow(&l) != nullptr);

    /* The steady state a long session settles into: pages are recycled off
     * the front as fast as they are added to the back. */
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
