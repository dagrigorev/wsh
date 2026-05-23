#pragma once
#ifndef WSH_SCHEDULER_H
#define WSH_SCHEDULER_H

#include <windows.h>
#include "wsh_bool.h"


#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_MAX 64
#define SCHED_LABEL_MAX 64

typedef enum {
    SCHED_PENDING,
    SCHED_RUNNING,
    SCHED_DONE,
    SCHED_FAILED
} SchedStatus;

typedef struct {
    int        id;
    DWORD      due_at;       /* absolute GetTickCount() value when this fires */
    DWORD      interval_ms;  /* 0 = one-shot, >0 = repeating interval */
    char      *command;      /* heap copy of command line */
    SchedStatus status;
    int        exit_code;
    char       label[SCHED_LABEL_MAX];
} SchedTask;

typedef struct {
    SchedTask tasks[SCHED_MAX];
    int       count;
    int       next_id;
    bool      tick_in_progress; /* guard against reentrance */
} Scheduler;

void scheduler_init(Scheduler *s);
void scheduler_free(Scheduler *s);

/* Add a one-shot task: execute command after delay_ms milliseconds.
 * Returns task id (>0) or -1 on failure. */
int  scheduler_add(Scheduler *s, DWORD delay_ms, const char *command);

/* Add a repeating task: execute command every interval_ms milliseconds.
 * Returns task id (>0) or -1 on failure. */
int  scheduler_add_repeating(Scheduler *s, DWORD interval_ms, const char *command);

/* Remove a task by id. Returns true if found and removed. */
bool scheduler_remove(Scheduler *s, int id);

/* Remove all tasks. */
void scheduler_clear(Scheduler *s);

/* Find a task by id. Returns NULL if not found. */
SchedTask *scheduler_find(Scheduler *s, int id);

/* Poll due tasks and execute them via ctx->io and shell_exec_line.
 * Returns the number of tasks executed this tick.
 * ctx must not be NULL. This function is NOT thread-safe. */
struct ShellContext;
int  scheduler_tick(Scheduler *s, struct ShellContext *ctx);


#ifdef __cplusplus
}
#endif

#endif /* WSH_SCHEDULER_H */
