#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "scheduler.h"
#include "shell_ctx.h"
#include "../core/str_util.h"
#include "../core/log.h"

void scheduler_init(Scheduler *s) {
    memset(s, 0, sizeof(*s));
    s->next_id = 1;
}

void scheduler_free(Scheduler *s) {
    for (int i = 0; i < SCHED_MAX; i++) {
        if (s->tasks[i].command) {
            HeapFree(GetProcessHeap(), 0, s->tasks[i].command);
        }
    }
    memset(s, 0, sizeof(*s));
}

static int scheduler_insert(Scheduler *s, DWORD delay_ms, const char *command, DWORD interval_ms) {
    if (!s || !command || !command[0]) return -1;
    if (s->count >= SCHED_MAX) return -1;

    int slot = -1;
    for (int i = 0; i < SCHED_MAX; i++) {
        if (!s->tasks[i].id) { slot = i; break; }
    }
    if (slot < 0) return -1;

    int id = s->next_id++;
    if (id <= 0) id = 1;
    s->next_id = id + 1;

    SchedTask *t = &s->tasks[slot];
    memset(t, 0, sizeof(*t));
    t->id          = id;
    t->due_at      = GetTickCount() + delay_ms;
    t->interval_ms = interval_ms;
    t->command     = str_dup(command);
    t->status      = SCHED_PENDING;

    /* Derive a short label from the command */
    const char *src = command;
    int di = 0;
    while (*src && di < SCHED_LABEL_MAX - 1) {
        if (*src == '\r' || *src == '\n') break;
        t->label[di++] = *src++;
    }
    t->label[di] = '\0';

    s->count++;
    return id;
}

int scheduler_add(Scheduler *s, DWORD delay_ms, const char *command) {
    return scheduler_insert(s, delay_ms, command, 0);
}

int scheduler_add_repeating(Scheduler *s, DWORD interval_ms, const char *command) {
    return scheduler_insert(s, interval_ms, command, interval_ms);
}

bool scheduler_remove(Scheduler *s, int id) {
    if (!s || id <= 0) return false;
    for (int i = 0; i < SCHED_MAX; i++) {
        if (s->tasks[i].id == id) {
            if (s->tasks[i].command) {
                HeapFree(GetProcessHeap(), 0, s->tasks[i].command);
            }
            memset(&s->tasks[i], 0, sizeof(s->tasks[i]));
            if (s->count > 0) s->count--;
            return true;
        }
    }
    return false;
}

void scheduler_clear(Scheduler *s) {
    scheduler_free(s);
    scheduler_init(s);
}

SchedTask *scheduler_find(Scheduler *s, int id) {
    if (!s || id <= 0) return NULL;
    for (int i = 0; i < SCHED_MAX; i++) {
        if (s->tasks[i].id == id) return &s->tasks[i];
    }
    return NULL;
}

int scheduler_tick(Scheduler *s, ShellContext *ctx) {
    if (!s || !ctx || s->tick_in_progress) return 0;
    if (s->count <= 0) return 0;

    s->tick_in_progress = true;
    DWORD now = GetTickCount();
    int executed = 0;

    for (int i = 0; i < SCHED_MAX; i++) {
        SchedTask *t = &s->tasks[i];
        if (!t->id || t->status != SCHED_PENDING) continue;

        /* Check if due: handle DWORD tick wraparound safely for small delays */
        int32_t diff = (int32_t)(now - t->due_at);
        if (diff < 0) continue;

        /* Task is due — execute it */
        t->status = SCHED_RUNNING;
        WSH_LOG_DEBUG("scheduler: executing task %d: %s", t->id, t->command);

        char output[512];
        _snprintf(output, sizeof(output), "\r\n[wsh scheduler] task #%d: %s\r\n",
                  t->id, t->label);
        io_write(ctx->io, output);

        int result = shell_exec_line(ctx, t->command);

        t->exit_code = result;
        t->status = result == 0 ? SCHED_DONE : SCHED_FAILED;

        char done_msg[128];
        _snprintf(done_msg, sizeof(done_msg),
                  "[wsh scheduler] task #%d done (exit %d)\r\n", t->id, result);
        io_write(ctx->io, done_msg);

        /* If repeating, reschedule */
        if (t->interval_ms > 0) {
            t->due_at = GetTickCount() + t->interval_ms;
            t->status = SCHED_PENDING;
            WSH_LOG_DEBUG("scheduler: task %d rescheduled in %lu ms",
                          t->id, (unsigned long)t->interval_ms);
        }

        executed++;
    }

    s->tick_in_progress = false;
    return executed;
}
