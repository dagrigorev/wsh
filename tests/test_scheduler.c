#include "test_helpers.h"
#include "../src/shell/scheduler.h"
#include "../src/shell/shell_ctx.h"
#include <string.h>

static int g_write_count = 0;
static char g_write_buf[65536];

static void test_write(struct IShellIO *self, const char *buf, int len) {
    (void)self;
    if (g_write_count + len < (int)sizeof(g_write_buf) - 1) {
        memcpy(g_write_buf + g_write_count, buf, (size_t)len);
        g_write_count += len;
        g_write_buf[g_write_count] = '\0';
    }
}

static int test_read_line(struct IShellIO *self, char *buf, int size) {
    (void)self; (void)buf; (void)size;
    return 0;
}

static struct IShellIO test_io = { test_write, test_read_line };
static ShellContext test_ctx;

static void setup(void) {
    memset(g_write_buf, 0, sizeof(g_write_buf));
    g_write_count = 0;
    shell_ctx_init(&test_ctx, &test_io);
}

static void teardown(void) {
    shell_ctx_free(&test_ctx);
}

TEST(Scheduler, InitAndFree) {
    Scheduler s;
    scheduler_init(&s);
    ASSERT_EQ(s.count, 0);
    ASSERT_EQ(s.next_id, 1);
    scheduler_free(&s);
}

TEST(Scheduler, AddSingleTask) {
    setup();
    Scheduler *s = &test_ctx.scheduler;
    int id = scheduler_add(s, 5000, "echo hello");
    ASSERT(id > 0);
    ASSERT_EQ(s->count, 1);
    SchedTask *t = scheduler_find(s, id);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->command, "echo hello");
    ASSERT(t->interval_ms == 0);
    teardown();
}

TEST(Scheduler, AddAndRemove) {
    setup();
    Scheduler *s = &test_ctx.scheduler;
    int id = scheduler_add(s, 5000, "echo hello");
    ASSERT(id > 0);
    ASSERT_EQ(s->count, 1);
    ASSERT_TRUE(scheduler_remove(s, id));
    ASSERT_EQ(s->count, 0);
    ASSERT_NULL(scheduler_find(s, id));
    teardown();
}

TEST(Scheduler, AddRepeating) {
    setup();
    Scheduler *s = &test_ctx.scheduler;
    int id = scheduler_add_repeating(s, 10000, "echo tick");
    ASSERT(id > 0);
    SchedTask *t = scheduler_find(s, id);
    ASSERT_NOT_NULL(t);
    ASSERT(t->interval_ms == 10000);
    teardown();
}

TEST(Scheduler, ClearAll) {
    setup();
    Scheduler *s = &test_ctx.scheduler;
    scheduler_add(s, 1000, "echo a");
    scheduler_add(s, 2000, "echo b");
    scheduler_add(s, 3000, "echo c");
    ASSERT_EQ(s->count, 3);
    scheduler_clear(s);
    ASSERT_EQ(s->count, 0);
    teardown();
}

TEST(Scheduler, QueueFull) {
    setup();
    Scheduler *s = &test_ctx.scheduler;
    int last_id = -1;
    for (int i = 0; i < SCHED_MAX + 5; i++) {
        int id = scheduler_add(s, 1000, "echo test");
        if (id > 0) last_id = id;
    }
    ASSERT_EQ(s->count, SCHED_MAX);
    ASSERT(last_id > 0);
    teardown();
}

TEST(Scheduler, RemoveNonexistent) {
    Scheduler s;
    scheduler_init(&s);
    ASSERT_FALSE(scheduler_remove(&s, 999));
    scheduler_free(&s);
}

TEST(Scheduler, ContextLifecycle) {
    setup();
    ASSERT(test_ctx.scheduler.count == 0);
    ASSERT(test_ctx.scheduler.next_id >= 1);
    teardown();
}
