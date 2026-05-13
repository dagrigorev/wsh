#pragma once
#ifndef WSH_HISTORY_H
#define WSH_HISTORY_H

#include <windows.h>
#include "wsh_bool.h"


#ifdef __cplusplus
extern "C" {
#endif

#define HISTORY_MAX 50000

typedef struct {
    char   **entries;       /* Ring buffer of heap-allocated strings */
    int      capacity;      /* Max entries (from HISTSIZE env var or HISTORY_MAX) */
    int      head;          /* Next write index */
    int      count;         /* Total entries stored */
    int      cursor;        /* Current navigation position (-1 = live input) */
    char    *search_pat;    /* Active Ctrl-R search pattern (heap, may be NULL) */
    int      search_idx;    /* Current search result index */
    char    *hist_file;     /* Path to ~/.Wsh_history (heap) */
    bool     ignore_dups;   /* HISTIGNORE_DUPS — skip consecutive duplicates */
    bool     share_history; /* Write to file on every command */
} History;

/* ─── API ────────────────────────────────────────────────────────────────── */

/* Initialize; loads HISTSIZE, HISTFILE from env */
void history_init(History *h);

/* Append an entry (silently ignores empty / duplicate if configured) */
void history_push(History *h, const char *cmd);

/* Navigation: returns string at cursor position or NULL at live input */
const char *history_prev(History *h);
const char *history_next(History *h);

/* Reset navigation cursor to live input position */
void history_reset_cursor(History *h);

/* Ctrl-R search: returns next match for pattern, or NULL if none */
const char *history_search_prev(History *h, const char *pattern);

/* History expansion: !! !n !str !$ !* — writes expanded into buf */
bool history_expand(History *h, const char *input, char *buf, int buf_size);

/* Persist new entries to file */
void history_save(History *h);

/* Load entries from file at startup */
void history_load(History *h);

/* Free all resources */
void history_free(History *h);

/* Return entry at index (0 = oldest valid) */
const char *history_at(const History *h, int index);

/* Number of entries */
int history_count(const History *h);


#ifdef __cplusplus
}
#endif

#endif /* WSH_HISTORY_H */
