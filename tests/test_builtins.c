/*
 * test_builtins.c — Built-in command unit tests.
 *
 * All output is captured via BufIO so tests can assert on the
 * exact strings produced without a window or PTY.
 */
#include "test_helpers.h"
#include "../src/core/str_util.h"
#include "../src/shell/shell_ctx.h"
#include "../src/shell/builtins.h"
#include <string.h>

/* ── Buffer IO ──────────────────────────────────────────────────────────────── */
typedef struct { IShellIO base; char buf[4096]; int len; } BufIO2;
static void b2_write(IShellIO *s, const char *d, int n) {
    BufIO2 *b = (BufIO2*)s;
    if (b->len + n < (int)sizeof(b->buf)-1) { memcpy(b->buf+b->len,d,n); b->len+=n; b->buf[b->len]='\0'; }
}
static int b2_read(IShellIO *s, char *b, int z) { (void)s;(void)b;(void)z; return 0; }

static ShellContext g_ctx;
static BufIO2       g_bio;

static void setup(void) {
    memset(&g_bio, 0, sizeof(g_bio));
    g_bio.base.write = b2_write; g_bio.base.read_line = b2_read;
    shell_ctx_init(&g_ctx, (IShellIO *)&g_bio);
}
static void teardown(void) { shell_ctx_free(&g_ctx); }

/* ── echo ───────────────────────────────────────────────────────────────────── */

TEST(Builtins, EchoBasic) {
    setup();
    char *argv[] = { "echo", "hello", "world", NULL };
    int ret = builtin_echo(3, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    /* Output should contain "hello world" */
    ASSERT_TRUE(strstr(g_bio.buf, "hello world") != NULL);
    teardown();
}

TEST(Builtins, EchoNoNewline) {
    setup();
    char *argv[] = { "echo", "-n", "no-nl", NULL };
    builtin_echo(3, argv, &g_ctx);
    /* Should NOT end with \r\n */
    int len = g_bio.len;
    ASSERT_TRUE(len > 0);
    ASSERT_FALSE(g_bio.buf[len-1] == '\n');
    teardown();
}

/* ── pwd ────────────────────────────────────────────────────────────────────── */

TEST(Builtins, Pwd) {
    setup();
    char *argv[] = { "pwd", NULL };
    int ret = builtin_pwd(1, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    /* Output should be non-empty */
    ASSERT_TRUE(g_bio.len > 0);
    teardown();
}

/* ── export / env ────────────────────────────────────────────────────────────── */

TEST(Builtins, ExportSetsVariable) {
    setup();
    char *argv[] = { "export", "MY_TEST_VAR=hello123", NULL };
    int ret = builtin_export(2, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    const char *val = shell_getenv(&g_ctx, "MY_TEST_VAR");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "hello123");
    teardown();
}

/* ── alias ───────────────────────────────────────────────────────────────────── */

TEST(Builtins, AliasSetAndGet) {
    setup();
    char *set_argv[] = { "alias", "ll='ls -lh'", NULL };
    builtin_alias(2, set_argv, &g_ctx);

    /* Get the alias back */
    char *get_argv[] = { "alias", "ll", NULL };
    g_bio.len = 0; g_bio.buf[0] = '\0';
    builtin_alias(2, get_argv, &g_ctx);
    ASSERT_TRUE(strstr(g_bio.buf, "ll") != NULL);
    ASSERT_TRUE(strstr(g_bio.buf, "ls -lh") != NULL);
    teardown();
}

/* ── test / [ ─────────────────────────────────────────────────────────────── */

TEST(Builtins, TestStringEqual) {
    setup();
    char *argv[] = { "test", "abc", "=", "abc", NULL };
    int ret = builtin_test(4, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    teardown();
}

TEST(Builtins, TestStringNotEqual) {
    setup();
    char *argv[] = { "test", "abc", "!=", "xyz", NULL };
    int ret = builtin_test(4, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    teardown();
}

TEST(Builtins, TestIntegerComparison) {
    setup();
    char *a[] = { "test", "10", "-gt", "5", NULL };
    ASSERT_EQ(builtin_test(4, a, &g_ctx), 0);
    char *b[] = { "test", "3",  "-lt", "2", NULL };
    ASSERT_EQ(builtin_test(4, b, &g_ctx), 1); /* 3 < 2 is false */
    teardown();
}

TEST(Builtins, TestEmptyString) {
    setup();
    char *argv_z[] = { "test", "-z", "", NULL };
    ASSERT_EQ(builtin_test(3, argv_z, &g_ctx), 0);  /* -z "" is true */
    char *argv_n[] = { "test", "-n", "x", NULL };
    ASSERT_EQ(builtin_test(3, argv_n, &g_ctx), 0);  /* -n "x" is true */
    teardown();
}

/* ── true / false ─────────────────────────────────────────────────────────── */

TEST(Builtins, TrueFalse) {
    setup();
    char *t[] = { "true", NULL };
    char *f[] = { "false", NULL };
    ASSERT_EQ(builtin_true_cmd(1, t, &g_ctx), 0);
    ASSERT_EQ(builtin_false_cmd(1, f, &g_ctx), 1);
    teardown();
}
