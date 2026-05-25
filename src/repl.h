#pragma once
/*
 * repl.h — Read-Eval-Print Loop for the built-in shell.
 *
 * The REPL owns the line-editing state (cursor, buffer, completion, history
 * navigation) and routes keyboard InputEvents from the window to the shell.
 *
 * Execution model:
 *   Commands are executed on a worker thread so the UI message loop is never
 *   blocked.  The completion callback (on_exec_done) is invoked from the worker
 *   thread after the command finishes.  The UI layer should forward this
 *   callback to the main thread (e.g. via PostMessage) before showing the
 *   prompt or updating state.
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

#include "wsh_bool.h"
#include "../shell/shell_ctx.h"
#include "../shell/completion.h"


#ifdef __cplusplus
extern "C" {
#endif

#define REPL_LINE_MAX 8192

/* ── REPL state ────────────────────────────────────────────────────────────── */

typedef struct Repl Repl;

/* Callback invoked from the worker thread when a command finishes execution.
 * The UI layer should forward this to the main thread (PostMessage) before
 * showing the prompt.  exec_result is the command's exit status. */
typedef void (*ReplExecDoneFn)(Repl *r, int exec_result);

struct Repl {
    ShellContext   *ctx;          /* Shell context (not owned) */
    char            prompt[1024]; /* last rendered prompt, needed for redraw */
    int             prompt_len;   /* byte length of prompt */
    int             prompt_cols;  /* display columns occupied by prompt */
    char            line[REPL_LINE_MAX];
    int             len;          /* current content length */
    int             cursor;       /* insertion point (0..len) */
    CompletionResult completion;
    bool            completing;   /* true = second Tab press pending */
    bool            hist_search;  /* Ctrl+R incremental search active */
    char            hist_pat[256];
    char            hist_save[REPL_LINE_MAX]; /* saved line buffer for cancel */
    volatile LONG   executing;    /* non-zero while command thread is running */
    HANDLE          exec_thread;  /* handle to the executing thread */
    char           *execute_line; /* owned copy of the line being executed */
    int             exec_result;  /* exit status of last command */
    ReplExecDoneFn  on_exec_done; /* completion callback (may be NULL) */

    /* AI reasoning overlay text (max 2 lines) */
    wchar_t         reasoning_text[2][256];
    bool            reasoning_dirty; /* set when reasoning needs update */
};

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

/* Request cancellation of the currently executing command.
 * Sets the cancel flag on the shell context and waits briefly for the
 * worker thread to exit.  If the thread does not exit within the timeout,
 * it is terminated.  Safe to call from any thread. */
void repl_cancel_exec(Repl *r);

/* Set the completion callback invoked (from the worker thread) when a
 * command finishes.  The UI layer should forward this to the main thread
 * before showing the prompt or mutating REPL state. */
void repl_set_on_exec_done(Repl *r, ReplExecDoneFn cb);

/* Get current line content. Returns pointer to internal buffer. */
static inline const char *repl_get_line(const Repl *r) { return r->line; }

/* Get current line length. */
static inline int repl_get_line_len(const Repl *r) { return r->len; }

/* Check if reasoning should be re-triggered (called from UI thread). */
static inline bool repl_is_reasoning_dirty(Repl *r) {
    bool d = r->reasoning_dirty;
    r->reasoning_dirty = false;
    return d;
}


#ifdef __cplusplus
}
#endif

#endif /* WSH_REPL_H */