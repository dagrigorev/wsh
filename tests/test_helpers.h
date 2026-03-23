#pragma once
/*
 * test_helpers.h — Minimal unit-test framework for Wsh.
 *
 * No external dependencies (no CUnit, no Google Test) — the test binary
 * is a plain Windows console app that returns 0 on success.
 *
 * Usage:
 *   ASSERT(expr)              — abort with message on failure
 *   ASSERT_EQ(a, b)           — assert a == b (integers)
 *   ASSERT_STR_EQ(a, b)       — assert strcmp(a, b) == 0
 *   ASSERT_NULL(p)            — assert p == NULL
 *   ASSERT_NOT_NULL(p)        — assert p != NULL
 *
 *   TEST(suite, name) { ... } — define a test case
 *   RUN_TESTS()               — run all registered tests, return pass/fail
 */
#ifndef WSH_TEST_HELPERS_H
#define WSH_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── Test registry ────────────────────────────────────────────────────────── */

typedef void (*TestFn)(void);

typedef struct {
    const char *suite;
    const char *name;
    TestFn      fn;
} TestCase;

/* Max 256 tests per binary */
#define MAX_TESTS 256
extern TestCase g_tests[MAX_TESTS];
extern int      g_test_count;
extern int      g_pass_count;
extern int      g_fail_count;

/* ── Assertion macros ─────────────────────────────────────────────────────── */

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL  %s:%d: assertion failed: %s\n", \
                __FILE__, __LINE__, #expr); \
        g_fail_count++; return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL  %s:%d: %s == %s  (%ld != %ld)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b); \
        g_fail_count++; return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL  %s:%d: strcmp(%s, %s)  (\"%s\" != \"%s\")\n", \
                __FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
        g_fail_count++; return; \
    } \
} while(0)

#define ASSERT_NULL(p)     ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_TRUE(x)     ASSERT(x)
#define ASSERT_FALSE(x)    ASSERT(!(x))

/* ── Test registration ────────────────────────────────────────────────────── */

#define TEST(suite_name, test_name) \
    static void _test_##suite_name##_##test_name(void); \
    static void __attribute__((constructor)) \
        _reg_##suite_name##_##test_name(void) { \
        if (g_test_count < MAX_TESTS) { \
            g_tests[g_test_count].suite = #suite_name; \
            g_tests[g_test_count].name  = #test_name; \
            g_tests[g_test_count].fn    = _test_##suite_name##_##test_name; \
            g_test_count++; \
        } \
    } \
    static void _test_##suite_name##_##test_name(void)

/* ── Runner ───────────────────────────────────────────────────────────────── */

/* Returns 0 if all tests pass, 1 otherwise.
 * Call from main(). */
int run_all_tests(void);

#endif /* WSH_TEST_HELPERS_H */
