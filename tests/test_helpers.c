#include "test_helpers.h"
#include <stdio.h>

TestCase g_tests[MAX_TESTS];
int      g_test_count = 0;
int      g_pass_count = 0;
int      g_fail_count = 0;

int run_all_tests(void) {
    printf("Running %d test(s)...\n", g_test_count);
    for (int i = 0; i < g_test_count; i++) {
        int fail_before = g_fail_count;
        printf("  [ RUN ] %s::%s\n", g_tests[i].suite, g_tests[i].name);
        g_tests[i].fn();
        if (g_fail_count == fail_before) {
            printf("  [ OK  ] %s::%s\n", g_tests[i].suite, g_tests[i].name);
            g_pass_count++;
        }
    }
    printf("\n%d passed, %d failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}

int main(void) { return run_all_tests(); }
