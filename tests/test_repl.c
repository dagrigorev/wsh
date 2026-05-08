#include "test_helpers.h"
#include "../src/repl.h"
#include "../src/shell/shell_ctx.h"
#include <string.h>

typedef struct { IShellIO base; char buf[4096]; int len; } BufIO;
static void buf_write(IShellIO *self, const char *buf, int len) {
    BufIO *io = (BufIO *)self;
    if (len < 0) len = (int)strlen(buf);
    if (io->len + len >= (int)sizeof(io->buf)) len = (int)sizeof(io->buf) - io->len - 1;
    memcpy(io->buf + io->len, buf, (size_t)len);
    io->len += len;
    io->buf[io->len] = '\0';
}
static int buf_read(IShellIO *self, char *buf, int size) { (void)self; (void)buf; (void)size; return 0; }

TEST(Repl, RedrawKeepsPromptAndInputOnSameLine) {
    BufIO io; memset(&io, 0, sizeof(io));
    io.base.write = buf_write;
    io.base.read_line = buf_read;

    ShellContext ctx;
    shell_ctx_init(&ctx, (IShellIO *)&io);
    shell_setenv(&ctx, "PROMPT", "wsh> ", false);

    Repl repl;
    repl_init(&repl, &ctx);
    repl_show_prompt(&repl);
    ASSERT_TRUE(strstr(io.buf, "wsh> ") != NULL);

    io.len = 0; io.buf[0] = '\0';
    ASSERT_TRUE(repl_handle_input(&repl, "a", 1));
    ASSERT_TRUE(strstr(io.buf, "\rwsh> a") != NULL);

    repl_free(&repl);
    shell_ctx_free(&ctx);
}
