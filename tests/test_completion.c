#include "test_helpers.h"
#include "../src/shell/completion.h"
#include "../src/shell/shell_ctx.h"
#include <string.h>

typedef struct { IShellIO base; char buf[256]; int len; } BufIO;
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

TEST(Completion, CompletesAliasName) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    ASSERT_EQ(shell_exec_line(&ctx, "alias gst='git status'"), 0);

    CompletionResult cr = completion_compute("gs", 2, &ctx);
    ASSERT_TRUE(cr.count >= 1);
    ASSERT_STR_EQ(cr.matches[0], "gst");
    completion_free(&cr);
    shell_ctx_free(&ctx);
}

TEST(Completion, CompletesEnvironmentVariables) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    shell_setenv(&ctx, "WSH_TEST_ENV", "123", false);

    CompletionResult cr = completion_compute("echo $WSH_T", 11, &ctx);
    ASSERT_TRUE(cr.count >= 1);
    ASSERT_STR_EQ(cr.matches[0], "$WSH_TEST_ENV");
    completion_free(&cr);
    shell_ctx_free(&ctx);
}

TEST(Completion, AppliesSelectedMatchIntoBuffer) {
    ShellContext ctx; BufIO io;
    make_ctx(&ctx, &io);
    ASSERT_EQ(shell_exec_line(&ctx, "alias gst='git status'"), 0);

    char line[64] = "gs";
    CompletionResult cr = completion_compute(line, 2, &ctx);
    int new_cursor = completion_apply(&cr, 0, line, 2, 2, (int)sizeof(line));
    ASSERT_STR_EQ(line, "gst");
    ASSERT_EQ(new_cursor, 3);
    completion_free(&cr);
    shell_ctx_free(&ctx);
}
