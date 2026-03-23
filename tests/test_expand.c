/*
 * test_expand.c — Word expansion unit tests.
 *
 * Uses a BufIO (buffer-capturing IShellIO) so the shell can run without
 * a window.  This demonstrates the Dependency Inversion Principle: the
 * shell doesn't care whether output goes to a renderer or a string buffer.
 */
#include "test_helpers.h"
#include "../src/core/str_util.h"
#include "../src/shell/shell_ctx.h"
#include "../src/shell/expand.h"
#include <string.h>
#include <stdio.h>

/* ── Buffer IO ─────────────────────────────────────────────────────────────── */

typedef struct {
    IShellIO base;
    char     buf[65536];
    int      len;
} BufIO;

static void bufio_write(IShellIO *self, const char *data, int n) {
    BufIO *b = (BufIO *)self;
    if (b->len + n < (int)sizeof(b->buf) - 1) {
        memcpy(b->buf + b->len, data, (size_t)n);
        b->len += n;
        b->buf[b->len] = '\0';
    }
}
static int bufio_read(IShellIO *self, char *buf, int sz) {
    (void)self; (void)buf; (void)sz; return 0;
}

static void bufio_init(BufIO *b) {
    memset(b, 0, sizeof(*b));
    b->base.write     = bufio_write;
    b->base.read_line = bufio_read;
}

/* ── Fixture ────────────────────────────────────────────────────────────────── */

static ShellContext g_ctx;
static BufIO        g_bufio;

static void fixture_setup(void) {
    bufio_init(&g_bufio);
    shell_ctx_init(&g_ctx, (IShellIO *)&g_bufio);
    shell_setenv(&g_ctx, "FOO",  "hello",  false);
    shell_setenv(&g_ctx, "NUM",  "42",     false);
    shell_setenv(&g_ctx, "EMPTY","",       false);
}
static void fixture_teardown(void) {
    shell_ctx_free(&g_ctx);
}

/* ── Tests ──────────────────────────────────────────────────────────────────── */

TEST(Expand, VariableSimple) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "$FOO");
    ASSERT_STR_EQ(r, "hello");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, VariableBraces) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "${FOO}_world");
    ASSERT_STR_EQ(r, "hello_world");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, DefaultValue) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "${UNSET:-default}");
    ASSERT_STR_EQ(r, "default");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, DefaultValueNotUsedWhenSet) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "${FOO:-default}");
    ASSERT_STR_EQ(r, "hello");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, StringLength) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "${#FOO}");
    ASSERT_STR_EQ(r, "5"); /* "hello" has 5 chars */
    str_free(r);
    fixture_teardown();
}

TEST(Expand, Arithmetic_Addition) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "$((3 + 4))");
    ASSERT_STR_EQ(r, "7");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, Arithmetic_Variable) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "$(($NUM * 2))");
    ASSERT_STR_EQ(r, "84");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, Arithmetic_Ternary) {
    fixture_setup();
    char *r = expand_string(&g_ctx, "$((1 ? 10 : 20))");
    ASSERT_STR_EQ(r, "10");
    str_free(r);
    r = expand_string(&g_ctx, "$((0 ? 10 : 20))");
    ASSERT_STR_EQ(r, "20");
    str_free(r);
    fixture_teardown();
}

TEST(Expand, Tilde) {
    fixture_setup();
    char *home = str_dup(shell_getenv(&g_ctx, "HOME"));
    char *r    = expand_tilde(&g_ctx, "~/foo/bar");
    ASSERT_NOT_NULL(r);
    /* Result should start with home */
    ASSERT_TRUE(str_startswith(r, home));
    str_free(r); str_free(home);
    fixture_teardown();
}

TEST(Expand, BraceComma) {
    WordList wl = expand_brace("pre{a,b,c}suf");
    ASSERT_EQ(wl.count, 3);
    ASSERT_STR_EQ(wl.words[0], "preasuf");
    ASSERT_STR_EQ(wl.words[1], "prebsuf");
    ASSERT_STR_EQ(wl.words[2], "precsuf");
    wordlist_free(&wl);
}

TEST(Expand, BraceNumericRange) {
    WordList wl = expand_brace("{1..5}");
    ASSERT_EQ(wl.count, 5);
    ASSERT_STR_EQ(wl.words[0], "1");
    ASSERT_STR_EQ(wl.words[4], "5");
    wordlist_free(&wl);
}

TEST(Expand, IFSSplit) {
    fixture_setup();
    shell_setenv(&g_ctx, "IFS", ":", false);
    WordList wl = expand_split_ifs(&g_ctx, "a:b:c");
    ASSERT_EQ(wl.count, 3);
    ASSERT_STR_EQ(wl.words[0], "a");
    ASSERT_STR_EQ(wl.words[1], "b");
    ASSERT_STR_EQ(wl.words[2], "c");
    wordlist_free(&wl);
    fixture_teardown();
}
