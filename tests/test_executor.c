#include "test_helpers.h"
#include "../src/shell/shell_ctx.h"
#include <string.h>

typedef struct { IShellIO base; char buf[512]; int len; } BufIO;
static void buf_write(IShellIO *self, const char *buf, int len) {
    BufIO *io = (BufIO *)self;
    if (len < 0) len = (int)strlen(buf);
    if (io->len + len >= (int)sizeof(io->buf)) len = (int)sizeof(io->buf) - io->len - 1;
    memcpy(io->buf + io->len, buf, (size_t)len);
    io->len += len;
    io->buf[io->len] = '\0';
}
static int buf_read(IShellIO *self, char *buf, int size) { (void)self; (void)buf; (void)size; return 0; }
static void make_ctx(ShellContext *ctx, BufIO *io) {
    memset(io, 0, sizeof(*io));
    io->base.write = buf_write;
    io->base.read_line = buf_read;
    shell_ctx_init(ctx, (IShellIO *)io);
}

TEST(Executor, ExecutesBuiltinEcho) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    int rc = shell_exec_line(&ctx, "echo one two");
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(strstr(io.buf, "one two") != NULL);
    shell_ctx_free(&ctx);
}

TEST(Executor, AppliesPureAssignmentsWithoutCommand) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    int rc = shell_exec_line(&ctx, "FOO=bar BAR='two words'");
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(shell_getenv(&ctx, "FOO"), "bar");
    ASSERT_STR_EQ(shell_getenv(&ctx, "BAR"), "two words");
    shell_ctx_free(&ctx);
}

TEST(Executor, ExpandsOneLevelOfAlias) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    ASSERT_EQ(shell_exec_line(&ctx, "alias hi='echo hello'"), 0);
    io.len = 0; io.buf[0] = '\0';
    int rc = shell_exec_line(&ctx, "hi world");
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(strstr(io.buf, "hello world") != NULL);
    shell_ctx_free(&ctx);
}
