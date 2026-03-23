#pragma once
/*
 * repl.h — Read-Eval-Print Loop for the built-in shell.
 *
 * The REPL owns the line-editing state (cursor, buffer, completion, history
 * navigation) and routes keyboard InputEvents from the window to the shell.
 *
 * Design patterns:
 *   Strategy   — IShellIO is injected; the REPL itself is the concrete impl.
 *   Observer   — precmd / preexec hooks fire before/after each command.
 *   Memento    — history_prev/next restores saved line buffer states.
 *
 * SOLID:
 *   S — Only line editing.  Shell execution stays in shell_ctx.
 *   I — Exposes only repl_handle_input() and repl_show_prompt() to main.c.
 */
#ifndef WSH_REPL_H
#define WSH_REPL_H

#include <stdbool.h>
#include "../shell/shell_ctx.h"
#include "../shell/completion.h"

#define REPL_LINE_MAX 8192

/* ── REPL state ────────────────────────────────────────────────────────────── */

typedef struct {
    ShellContext   *ctx;          /* Shell context (not owned) */
    char            line[REPL_LINE_MAX];
    int             len;          /* current content length */
    int             cursor;       /* insertion point (0..len) */
    CompletionResult completion;
    bool            completing;   /* true = second Tab press pending */
    bool            hist_search;  /* Ctrl+R incremental search active */
    char            hist_pat[256];
} Repl;

/* ── API ───────────────────────────────────────────────────────────────────── */

/* Initialise REPL with a live shell context. */
void repl_init(Repl *r, ShellContext *ctx);

/* Clean up REPL state (does NOT free ctx). */
void repl_free(Repl *r);

/* Render the prompt via ctx->io. */
void repl_show_prompt(Repl *r);

/* Feed a byte sequence (from keyboard or paste) into the REPL.
 * Returns true if the REPL consumed the input.
 * Returns false if an 'exit' was requested (caller should quit). */
bool repl_handle_input(Repl *r, const char *bytes, int len);

/* Re-draw the current line (CR, erase-to-EOL, reprint, reposition cursor). */
void repl_redraw_line(Repl *r);

#endif /* WSH_REPL_H */
