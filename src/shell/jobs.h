#pragma once
#ifndef WSH_JOBS_H
#define WSH_JOBS_H

#include <windows.h>
#include <stdbool.h>

#define JOBS_MAX 64

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE,
} JobStatus;

typedef struct {
    int       id;           /* Job number, 1-based */
    DWORD     pid;
    HANDLE    hprocess;
    JobStatus status;
    int       exit_code;
    char     *cmdline;      /* heap copy of original command line */
    bool      is_fg;        /* currently in foreground */
} Job;

typedef struct {
    Job  jobs[JOBS_MAX];
    int  count;
    int  fg_job_id;         /* 0 = no foreground job */
} JobTable;

/* ─── API ────────────────────────────────────────────────────────────────── */

void job_table_init(JobTable *jt);
void job_table_free(JobTable *jt);

/* Add a new background job; returns job id */
int  job_add(JobTable *jt, DWORD pid, HANDLE hprocess, const char *cmdline);

/* Remove a job by id */
void job_remove(JobTable *jt, int id);

/* Find a job by id */
Job *job_find(JobTable *jt, int id);

/* Update status of all jobs (poll via WaitForSingleObject) */
void job_poll_all(JobTable *jt);

/* Print job table to stdout-equivalent */
void job_print_all(JobTable *jt);

/* Bring job to foreground: waits for it to finish */
int  job_fg(JobTable *jt, int id);

/* Resume job in background */
bool job_bg(JobTable *jt, int id);

/* Kill job */
bool job_kill(JobTable *jt, int id, int signum);

#endif /* WSH_JOBS_H */
