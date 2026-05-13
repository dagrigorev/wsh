#pragma once
#ifndef WSH_COMPLETION_H
#define WSH_COMPLETION_H

#include "wsh_bool.h"
#include "shell_ctx.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ─── Completion result ──────────────────────────────────────────────────── */

typedef struct {
    char **matches;              /* Heap array of heap strings */
    int    count;
    int    selected;             /* Menu selection index */
    int    common_prefix_len;    /* Length of common prefix in all matches */
    bool   menu_active;          /* True when multi-match menu is showing */
} CompletionResult;

/* ─── API ────────────────────────────────────────────────────────────────── */

/* Compute completions for the current line at cursor_pos.
   Returns result; caller must call completion_free when done. */
CompletionResult completion_compute(const char *line, int cursor_pos,
                                    const ShellContext *ctx);

/* Free a completion result */
void completion_free(CompletionResult *cr);

/* Apply selected completion to line buffer.
   Returns new cursor position. */
int completion_apply(const CompletionResult *cr, int selected,
                     char *line_buf, int line_len, int cursor_pos,
                     int buf_size);


#ifdef __cplusplus
}
#endif

#endif /* WSH_COMPLETION_H */
