#pragma once
/*
 * shell_ctx.h — Shell execution context.
 *
 * ShellContext is the root object passed through every shell subsystem.
 * It uses a callback interface (IShellIO) for all I/O — this is the
 * Dependency Inversion Principle: the shell depends on the IShellIO
 * abstraction, not on a concrete renderer or ConPTY handle.
 *
 * Design patterns used:
 *   - Strategy (IShellIO): swap I/O backend without touching shell logic.
 *   - Composite (EnvScope chain): lexical scoping via linked list of scopes.
 *   - Command (ASTNode): each node is an executable command object.
 *
 * SOLID mapping:
 *   S — ShellContext manages execution state; env/history/jobs are delegates.
 *   O — New built-ins added to the dispatch table; ShellContext unchanged.
 *   D — All I/O via IShellIO*, not concrete handles.
 */
#ifndef WSH_SHELL_CTX_H
#define WSH_SHELL_CTX_H

#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include "env.h"
#include "history.h"
#include "jobs.h"
#include "../core/arena.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ── I/O Strategy interface (Dependency Inversion) ────────────────────────── */
/*
 * IShellIO abstracts all shell I/O.  The terminal renderer implements this
 * interface by routing bytes through the VT parser.  Tests implement it by
 * capturing to a buffer.  This keeps the shell unit-testable without a window.
 */
typedef struct IShellIO {
    /* Write 'len' bytes to the shell's standard output. */
    void (*write)(struct IShellIO *self, const char *buf, int len);

    /* Read up to 'size-1' bytes of a line from standard input.
     * Returns bytes read (may be 0 for EOF). */
    int  (*read_line)(struct IShellIO *self, char *buf, int size);
} IShellIO;

/* Convenience: write a NUL-terminated string. */
static inline void io_write(IShellIO *io, const char *s) {
    if (io && s) io->write(io, s, (int)strlen(s));
}
static inline void io_writeln(IShellIO *io, const char *s) {
    io_write(io, s); io_write(io, "\r\n");
}

/* ── Shell option flags ────────────────────────────────────────────────────── */

typedef struct {
    unsigned auto_cd        : 1;
    unsigned correct        : 1;  /* spell correction */
    unsigned hist_ignore_dups: 1;
    unsigned share_history  : 1;
    unsigned no_clobber     : 1;  /* noclobber */
    unsigned err_exit       : 1;  /* set -e */
    unsigned xtrace         : 1;  /* set -x */
    unsigned nounset        : 1;  /* set -u */
    unsigned interactive    : 1;
    unsigned glob_star      : 1;  /* ** recursive glob */
} ShellOptions;

/* ── Alias and function registries ────────────────────────────────────────── */

typedef struct Alias {
    char        *name;
    char        *value;
    struct Alias *next;
} Alias;

/* Forward-declare ASTNode so shell_func.h doesn't need to include parser.h */
struct ASTNode;

typedef struct ShellFunc {
    char            *name;
    struct ASTNode  *body;   /* owns the AST subtree */
    struct ShellFunc *next;
} ShellFunc;

/* ── Trap table ────────────────────────────────────────────────────────────── */

#define TRAP_COUNT 32   /* signals 0-31; 0 = EXIT pseudo-signal */

/* ── Shell context ─────────────────────────────────────────────────────────── */

typedef struct ShellContext {
    /* I/O strategy — set before calling any shell API */
    IShellIO     *io;

    /* Variable environment (lexical scope chain) */
    EnvScope     *env;

    /* Alias and function tables */
    Alias        *aliases;
    ShellFunc    *functions;

    /* Persistent subsystems */
    History       history;
    JobTable      jobs;

    /* Shell options */
    ShellOptions  opts;

    /* Per-command arena (reset after each top-level exec) */
    Arena        *arena;

    /* Positional parameters ($1 .. $9, $@, $#) */
    char        **positionals;
    int           positional_count;

    /* Special variables */
    int           last_status;    /* $? */
    DWORD         shell_pid;      /* $$ */
    DWORD         last_bg_pid;    /* $! */
    char          cwd[MAX_PATH];  /* current working directory */

    /* Trap handlers: NULL = default, "" = ignore, else command string */
    char         *traps[TRAP_COUNT];

    /* Exit request: set by 'exit' built-in */
    /* Win32 I/O handles for child processes (managed by executor) */
    HANDLE       h_stdin;
    HANDLE       h_stdout;
    HANDLE       h_stderr;

    bool          exit_requested;
    int           exit_code;

    /* Depth counter for recursion guard (source, eval) */
    int           call_depth;
} ShellContext;

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

/* Initialise ctx with the given I/O strategy.
 * io must remain valid for the lifetime of ctx. */
void shell_ctx_init(ShellContext *ctx, IShellIO *io);
void shell_ctx_free(ShellContext *ctx);

/* ── Execution API ─────────────────────────────────────────────────────────── */

/* Execute a NUL-terminated command line; returns exit status. */
int  shell_exec_line(ShellContext *ctx, const char *line);

/* Source a script file; returns last exit status. */
int  shell_source(ShellContext *ctx, const char *path);

/* Expand prompt format string (%~, %n, %m, %?, %#, etc.). Caller frees. */
char *shell_expand_prompt(ShellContext *ctx, const char *fmt);

/* Resolve a command name to its full path.  Caller frees. Returns NULL if
 * not found. */
char *shell_which(ShellContext *ctx, const char *name);

/* ── Environment helpers (thin wrappers; env.h is the authority) ─────────── */

const char *shell_getenv(const ShellContext *ctx, const char *name);
void        shell_setenv(ShellContext *ctx, const char *name,
                         const char *value, bool exported);
void        shell_unsetenv(ShellContext *ctx, const char *name);


#ifdef __cplusplus
}
#endif

#endif /* WSH_SHELL_CTX_H */
