#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"

/* ─── Internal helpers ───────────────────────────────────────────────────── */

/* Return pointer to entry at logical index (0 = most recent) */
static char *entry_at(const History *h, int offset) {
    if (offset < 0 || offset >= h->count) return NULL;
    int idx = ((h->head - 1 - offset) % h->capacity + h->capacity) % h->capacity;
    return h->entries[idx];
}

/* ─── Init ───────────────────────────────────────────────────────────────── */

void history_init(History *h) {
    memset(h, 0, sizeof(*h));

    /* Read HISTSIZE */
    char env_buf[32] = {0};
    h->capacity = HISTORY_MAX;
    if (GetEnvironmentVariableA("HISTSIZE", env_buf, sizeof(env_buf)) && atoi(env_buf) > 0)
        h->capacity = atoi(env_buf);
    if (h->capacity > HISTORY_MAX) h->capacity = HISTORY_MAX;

    h->entries = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    (size_t)h->capacity * sizeof(char *));

    /* Resolve history file */
    char histfile[MAX_PATH] = {0};
    if (!GetEnvironmentVariableA("HISTFILE", histfile, MAX_PATH)) {
        char profile[MAX_PATH] = {0};
        GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
        _snprintf(histfile, MAX_PATH, "%s\\.Wsh_history", profile);
    }
    h->hist_file = str_dup(histfile);
    h->cursor    = -1; /* live input */

    /* Options */
    char opt[32] = {0};
    if (GetEnvironmentVariableA("HISTIGNORE_DUPS", opt, sizeof(opt)))
        h->ignore_dups = true;

    history_load(h);
}

/* ─── Push ───────────────────────────────────────────────────────────────── */

void history_push(History *h, const char *cmd) {
    if (!cmd || !cmd[0]) return;

    /* Trim trailing whitespace */
    char *dup = str_dup(cmd);
    str_trim(dup);
    if (!dup[0]) { HeapFree(GetProcessHeap(), 0, dup); return; }

    /* Ignore duplicate of most recent */
    if (h->ignore_dups && h->count > 0) {
        char *last = entry_at(h, 0);
        if (last && strcmp(last, dup) == 0) {
            HeapFree(GetProcessHeap(), 0, dup);
            return;
        }
    }

    /* Free old entry if ring buffer is full */
    if (h->entries[h->head]) {
        HeapFree(GetProcessHeap(), 0, h->entries[h->head]);
        h->entries[h->head] = NULL;
    }

    h->entries[h->head] = dup;
    h->head = (h->head + 1) % h->capacity;
    if (h->count < h->capacity) h->count++;
    h->total_commands++;

    history_reset_cursor(h);

    /* Append to file */
    if (h->hist_file && h->share_history) {
        FILE *f = fopen(h->hist_file, "a");
        if (f) { fprintf(f, "%s\n", dup); fclose(f); }
    }
}

/* ─── Navigation ─────────────────────────────────────────────────────────── */

const char *history_prev(History *h) {
    if (h->count == 0) return NULL;
    int next_cursor = h->cursor + 1;
    if (next_cursor >= h->count) return NULL; /* at oldest */
    h->cursor = next_cursor;
    return entry_at(h, h->cursor);
}

const char *history_next(History *h) {
    if (h->cursor <= 0) { h->cursor = -1; return NULL; }
    h->cursor--;
    return entry_at(h, h->cursor);
}

void history_reset_cursor(History *h) {
    h->cursor = -1;
    if (h->search_pat) { HeapFree(GetProcessHeap(), 0, h->search_pat); h->search_pat = NULL; }
    h->search_idx = 0;
}

/* ─── Search (Ctrl-R) ────────────────────────────────────────────────────── */

const char *history_search_prev(History *h, const char *pattern) {
    if (!pattern || !pattern[0]) return NULL;
    /* Search backward from search_idx */
    for (int i = h->search_idx; i < h->count; i++) {
        char *e = entry_at(h, i);
        if (e && strstr(e, pattern)) {
            h->search_idx = i + 1;
            h->cursor     = i;
            return e;
        }
    }
    return NULL; /* No (more) matches */
}

/* ─── History Expansion ──────────────────────────────────────────────────── */

bool history_expand(History *h, const char *input, char *buf, int buf_size) {
    if (!input || input[0] != '!') {
        strncpy(buf, input ? input : "", buf_size - 1);
        return false;
    }
    const char *p = input + 1;

    /* !! — repeat last command */
    if (*p == '!') {
        const char *last = entry_at(h, 0);
        if (!last) return false;
        strncpy(buf, last, buf_size - 1);
        return true;
    }
    /* !$ — last argument of last command */
    if (*p == '$') {
        const char *last = entry_at(h, 0);
        if (!last) return false;
        const char *sp = strrchr(last, ' ');
        strncpy(buf, sp ? sp + 1 : last, buf_size - 1);
        return true;
    }
    /* !* — all args of last command */
    if (*p == '*') {
        const char *last = entry_at(h, 0);
        if (!last) return false;
        const char *sp = strchr(last, ' ');
        strncpy(buf, sp ? sp + 1 : "", buf_size - 1);
        return true;
    }
    /* !n — nth history entry (absolute command number, 1 = oldest) */
    if (*p >= '0' && *p <= '9') {
        int idx = atoi(p);
        int offset = h->total_commands - idx;
        const char *e = entry_at(h, offset);
        if (!e) return false;
        strncpy(buf, e, buf_size - 1);
        return true;
    }
    /* !str — most recent starting with str */
    for (int i = 0; i < h->count; i++) {
        char *e = entry_at(h, i);
        if (e && str_startswith(e, p)) {
            strncpy(buf, e, buf_size - 1);
            return true;
        }
    }
    return false;
}

/* ─── Persistence ────────────────────────────────────────────────────────── */

void history_save(History *h) {
    if (!h->hist_file) return;
    FILE *f = fopen(h->hist_file, "w");
    if (!f) return;
    /* Write oldest → newest */
    for (int i = h->count - 1; i >= 0; i--) {
        char *e = entry_at(h, i);
        if (e) fprintf(f, "%s\n", e);
    }
    fclose(f);
}

void history_load(History *h) {
    if (!h->hist_file) return;
    FILE *f = fopen(h->hist_file, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (line[0]) history_push(h, line);
    }
    fclose(f);
    history_reset_cursor(h);
}

/* ─── Access ─────────────────────────────────────────────────────────────── */

const char *history_at(const History *h, int index) {
    return entry_at(h, index);
}

int history_count(const History *h) {
    return h->count;
}

/* ─── Free ───────────────────────────────────────────────────────────────── */

void history_free(History *h) {
    history_save(h);
    for (int i = 0; i < h->capacity; i++) {
        if (h->entries[i]) HeapFree(GetProcessHeap(), 0, h->entries[i]);
    }
    HeapFree(GetProcessHeap(), 0, h->entries);
    if (h->hist_file) HeapFree(GetProcessHeap(), 0, h->hist_file);
    if (h->search_pat) HeapFree(GetProcessHeap(), 0, h->search_pat);
    memset(h, 0, sizeof(*h));
}
