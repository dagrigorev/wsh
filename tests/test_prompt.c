#include "test_helpers.h"
#include "../src/core/str_util.h"
#include "../src/shell/shell_ctx.h"
#include <string.h>

typedef struct { IShellIO base; char buf[64]; int len; } NullIO;
static void null_write(IShellIO *self, const char *buf, int len) { (void)self; (void)buf; (void)len; }
static int null_read(IShellIO *self, char *buf, int size) { (void)self; (void)buf; (void)size; return 0; }

static void make_ctx(ShellContext *ctx, NullIO *io) {
    memset(io, 0, sizeof(*io));
    io->base.write = null_write;
    io->base.read_line = null_read;
    shell_ctx_init(ctx, (IShellIO *)io);
}

TEST(Prompt, SupportsUserAndCwd) {
    ShellContext ctx; NullIO io;
    make_ctx(&ctx, &io);
    char *s = shell_expand_prompt(&ctx, "%n:%~ %# ");
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(strchr(s, ':') != NULL);
    ASSERT_TRUE(strstr(s, "# ") != NULL || strstr(s, "$ ") != NULL);
    str_free(s);
    shell_ctx_free(&ctx);
}

TEST(Prompt, SupportsLastStatus) {
    ShellContext ctx; NullIO io;
    make_ctx(&ctx, &io);
    ctx.last_status = 17;
    char *s = shell_expand_prompt(&ctx, "[%?]");
    ASSERT_STR_EQ(s, "[17]");
    str_free(s);
    shell_ctx_free(&ctx);
}
