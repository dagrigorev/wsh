/*
 * test_arena.c — Unit tests for the Arena allocator.
 *
 * Tests verify:
 *   - Basic alloc/reset cycle
 *   - Zero-initialisation guarantee
 *   - Growth beyond initial capacity
 *   - arena_strdup correctness
 *   - Multiple reset cycles (no memory corruption)
 */
#include "test_helpers.h"
#include "../src/core/arena.h"
#include <string.h>

TEST(Arena, BasicAllocAndStrdup) {
    Arena *a = arena_create(1024);
    ASSERT_NOT_NULL(a);

    char *s = arena_strdup(a, "hello");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");

    int *n = (int *)arena_alloc(a, sizeof(int));
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(*n, 0);   /* zero-initialised */
    *n = 42;
    ASSERT_EQ(*n, 42);

    arena_destroy(a);
}

TEST(Arena, GrowthBeyondInitialCapacity) {
    Arena *a = arena_create(64); /* deliberately tiny */
    ASSERT_NOT_NULL(a);

    /* Allocate 100 × 32-byte blocks — forces multiple chunk allocations */
    for (int i = 0; i < 100; i++) {
        char *p = (char *)arena_alloc(a, 32);
        ASSERT_NOT_NULL(p);
        memset(p, (char)i, 32);
    }
    arena_destroy(a);
}

TEST(Arena, ResetAndReuse) {
    Arena *a = arena_create(512);
    ASSERT_NOT_NULL(a);

    char *p1 = arena_strdup(a, "first");
    ASSERT_STR_EQ(p1, "first");

    arena_reset(a);

    /* After reset, new allocations start from same block */
    char *p2 = arena_strdup(a, "second");
    ASSERT_NOT_NULL(p2);
    ASSERT_STR_EQ(p2, "second");

    /* p1 is now invalid (undefined), but no crash should occur */
    arena_destroy(a);
}

TEST(Arena, MultipleResetCycles) {
    Arena *a = arena_create(256);
    for (int cycle = 0; cycle < 1000; cycle++) {
        for (int i = 0; i < 10; i++) {
            void *p = arena_alloc(a, 16);
            ASSERT_NOT_NULL(p);
        }
        arena_reset(a);
    }
    arena_destroy(a);
}
