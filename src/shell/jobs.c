#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jobs.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"

void job_table_init(JobTable *jt) {
    memset(jt, 0, sizeof(*jt));
}

void job_table_free(JobTable *jt) {
    for (int i = 0; i < JOBS_MAX; i++) {
        if (jt->jobs[i].id) {
            if (jt->jobs[i].hprocess) CloseHandle(jt->jobs[i].hprocess);
            if (jt->jobs[i].cmdline)  HeapFree(GetProcessHeap(), 0, jt->jobs[i].cmdline);
        }
    }
    memset(jt, 0, sizeof(*jt));
}

int job_add(JobTable *jt, DWORD pid, HANDLE hprocess, const char *cmdline) {
    /* Find a free slot */
    int id = 0;
    for (int i = 0; i < JOBS_MAX; i++) {
        if (!jt->jobs[i].id) {
            /* Assign lowest unused job number */
            id = i + 1;
            break;
        }
    }
    if (!id) { WSH_LOG_WARN("Job table full"); return -1; }

    Job *j   = &jt->jobs[id - 1];
    j->id        = id;
    j->pid        = pid;
    j->hprocess   = hprocess;
    j->status     = JOB_RUNNING;
    j->exit_code  = 0;
    j->cmdline    = str_dup(cmdline ? cmdline : "");
    j->is_fg      = false;
    jt->count++;
    return id;
}

void job_remove(JobTable *jt, int id) {
    if (id < 1 || id > JOBS_MAX) return;
    Job *j = &jt->jobs[id - 1];
    if (!j->id) return;
    if (j->hprocess) { CloseHandle(j->hprocess); j->hprocess = NULL; }
    if (j->cmdline)  { HeapFree(GetProcessHeap(), 0, j->cmdline); j->cmdline = NULL; }
    j->id = 0;
    if (jt->count > 0) jt->count--;
}

Job *job_find(JobTable *jt, int id) {
    if (id < 1 || id > JOBS_MAX) return NULL;
    Job *j = &jt->jobs[id - 1];
    return j->id ? j : NULL;
}

void job_poll_all(JobTable *jt) {
    for (int i = 0; i < JOBS_MAX; i++) {
        Job *j = &jt->jobs[i];
        if (!j->id || !j->hprocess) continue;
        if (j->status == JOB_DONE) continue;

        DWORD code = 0;
        DWORD wait = WaitForSingleObject(j->hprocess, 0);
        if (wait == WAIT_OBJECT_0) {
            GetExitCodeProcess(j->hprocess, &code);
            j->exit_code = (int)code;
            j->status    = JOB_DONE;
        }
    }
}

void job_print_all(JobTable *jt) {
    for (int i = 0; i < JOBS_MAX; i++) {
        Job *j = &jt->jobs[i];
        if (!j->id) continue;
        const char *status_str = "Running";
        if (j->status == JOB_STOPPED) status_str = "Stopped";
        if (j->status == JOB_DONE)    status_str = "Done";
        printf("[%d] %s\t%s\n", j->id, status_str, j->cmdline ? j->cmdline : "");
    }
}

int job_fg(JobTable *jt, int id) {
    Job *j = job_find(jt, id);
    if (!j || !j->hprocess) return -1;
    j->is_fg = true;
    jt->fg_job_id = id;
    if (j->status == JOB_STOPPED) {
        /* Resume — no true SIGCONT on Windows, but we can resume threads */
        j->status = JOB_RUNNING;
    }
    printf("%s\n", j->cmdline ? j->cmdline : "");
    DWORD code = 0;
    WaitForSingleObject(j->hprocess, INFINITE);
    GetExitCodeProcess(j->hprocess, &code);
    j->exit_code  = (int)code;
    j->status     = JOB_DONE;
    j->is_fg      = false;
    jt->fg_job_id = 0;
    return j->exit_code;
}

bool job_bg(JobTable *jt, int id) {
    Job *j = job_find(jt, id);
    if (!j) return false;
    j->is_fg  = false;
    j->status = JOB_RUNNING;
    printf("[%d] %s &\n", j->id, j->cmdline ? j->cmdline : "");
    return true;
}

bool job_kill(JobTable *jt, int id, int signum) {
    Job *j = job_find(jt, id);
    if (!j || !j->hprocess) return false;
    (void)signum; /* Windows doesn't have POSIX signals — TerminateProcess for all */
    TerminateProcess(j->hprocess, (UINT)signum);
    j->status = JOB_DONE;
    return true;
}
