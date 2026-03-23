#include "test_helpers.h"
#include "../src/shell/history.h"
#include <string.h>

TEST(History, PushAndNav) {
    History h; history_init(&h);
    /* Clear entries loaded from ~/.Wsh_history for a known-empty baseline. */
    h.count = 0;
    h.head  = 0;

    history_push(&h, "cmd1");
    history_push(&h, "cmd2");
    history_push(&h, "cmd3");

    /* Navigate backwards */
    ASSERT_STR_EQ(history_prev(&h), "cmd3");
    ASSERT_STR_EQ(history_prev(&h), "cmd2");
    ASSERT_STR_EQ(history_prev(&h), "cmd1");
    ASSERT_NULL(history_prev(&h));   /* at beginning */

    /* Navigate forward */
    ASSERT_STR_EQ(history_next(&h), "cmd2");
    ASSERT_STR_EQ(history_next(&h), "cmd3");
    ASSERT_NULL(history_next(&h));   /* at end */

    history_free(&h);
}

TEST(History, IgnoreDuplicates) {
    History h; history_init(&h);
    h.ignore_dups = true;
    /* Clear any entries loaded from ~/.Wsh_history so the test
     * starts from a known-empty state regardless of the user's history file. */
    h.count = 0;
    h.head  = 0;

    history_push(&h, "ls");
    history_push(&h, "ls");  /* duplicate — should be skipped */
    history_push(&h, "ls");

    /* Only one entry should exist */
    ASSERT_STR_EQ(history_prev(&h), "ls");
    ASSERT_NULL(history_prev(&h));

    history_free(&h);
}

TEST(History, BangExpansion) {
    History h; history_init(&h);
    history_push(&h, "echo first");
    history_push(&h, "ls -la");

    char out[256];
    /* !! repeats last command */
    ASSERT_TRUE(history_expand(&h, "!!", out, sizeof(out)));
    ASSERT_STR_EQ(out, "ls -la");

    /* !e repeats last starting with 'e' */
    ASSERT_TRUE(history_expand(&h, "!e", out, sizeof(out)));
    ASSERT_STR_EQ(out, "echo first");

    history_free(&h);
}
