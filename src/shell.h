#pragma once
#ifndef WSH_SHELL_H
#define WSH_SHELL_H

#include <windows.h>
#include <stdbool.h>
#include "util.h"
#include "history.h"
#include "jobs.h"

/* ─── Token types ────────────────────────────────────────────────────────── */

typedef enum {
    TOK_WORD, TOK_ASSIGN, TOK_PIPE, TOK_PIPE_ERR,
    TOK_REDIR_IN, TOK_REDIR_OUT, TOK_REDIR_APPEND,
    TOK_REDIR_HEREDOC, TOK_AND, TOK_OR, TOK_SEMI,
    TOK_NEWLINE, TOK_BG, TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE, TOK_BANG, TOK_EOF,
    TOK_DSEMI,          /* ;; for case */
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_ELIF, TOK_FI,
    TOK_WHILE, TOK_DO, TOK_DONE,
    TOK_FOR, TOK_IN,
    TOK_CASE, TOK_ESAC,
    TOK_FUNCTION,
} TokenKind;

typedef struct {
    TokenKind kind;
    char     *text;   /* Heap-allocated word text (for TOK_WORD, TOK_ASSIGN) */
    int       lineno;
} Token;

/* ─── Redirection ────────────────────────────────────────────────────────── */

typedef enum {
    REDIR_IN,      /* < */
    REDIR_OUT,     /* > */
    REDIR_APPEND,  /* >> */
    REDIR_HEREDOC, /* << */
    REDIR_FD,      /* n>&m */
} RedirKind;

typedef struct Redir {
    RedirKind    kind;
    int          fd;         /* LHS fd (default 0/1) */
    char        *target;     /* filename or heredoc text */
    int          target_fd;  /* for fd dup */
    struct Redir *next;
} Redir;

/* ─── AST ────────────────────────────────────────────────────────────────── */

typedef enum {
    NODE_CMD, NODE_PIPE, NODE_AND, NODE_OR,
    NODE_SEQ, NODE_BG, NODE_SUBSHELL,
    NODE_IF, NODE_WHILE, NODE_UNTIL, NODE_FOR, NODE_CASE,
    NODE_FUNCTION, NODE_REDIR, NODE_ASSIGN,
    NODE_ARITH,  /* (( expr )) */
} NodeKind;

typedef struct ASTNode {
    NodeKind kind;
    union {
        /* NODE_CMD */
        struct { char **argv; int argc; Redir *redirs; bool is_builtin; } cmd;
        /* NODE_PIPE, NODE_AND, NODE_OR, NODE_SEQ */
        struct { struct ASTNode *left, *right; } binary;
        /* NODE_IF */
        struct { struct ASTNode *cond, *body, *alt; } ifnode;
        /* NODE_WHILE / NODE_UNTIL */
        struct { struct ASTNode *cond, *body; } whilenode;
        /* NODE_FOR */
        struct { char *var; char **words; int word_count; struct ASTNode *body; } fornode;
        /* NODE_BG, NODE_SUBSHELL, NODE_REDIR */
        struct { struct ASTNode *child; Redir *redirs; } wrap;
        /* NODE_FUNCTION */
        struct { char *name; struct ASTNode *body; } func;
        /* NODE_ASSIGN */
        struct { char *name; char *value; } assign;
        /* NODE_ARITH */
        struct { char *expr; } arith;
    };
} ASTNode;

/* ─── Environment / Variable Store ──────────────────────────────────────── */

typedef struct EnvVar {
    char        *name;
    char        *value;
    bool         exported;
    bool         readonly;
    bool         integer;
    struct EnvVar *next;
} EnvVar;

typedef struct EnvScope {
    EnvVar       *vars;
    struct EnvScope *parent;
} EnvScope;

/* ─── Alias ──────────────────────────────────────────────────────────────── */

typedef struct Alias {
    char        *name;
    char        *value;
    struct Alias *next;
} Alias;

/* ─── Shell Function ─────────────────────────────────────────────────────── */

typedef struct ShellFunc {
    char          *name;
    ASTNode       *body;      /* owned by this struct */
    struct ShellFunc *next;
} ShellFunc;

/* ─── Shell Options (setopt/shopt flags) ─────────────────────────────────── */

typedef struct {
    bool auto_cd;
    bool correct;
    bool glob_star_short;   /* ** recursive glob */
    bool hist_ignore_dups;
    bool share_history;
    bool no_clobber;        /* noclobber */
    bool err_exit;          /* -e */
    bool xtrace;            /* -x */
    bool nounset;           /* -u */
    bool interactive;
} ShellOptions;

/* ─── Exec Context ───────────────────────────────────────────────────────── */

typedef struct ShellContext {
    EnvScope    *env;           /* Variable scopes */
    Alias       *aliases;       /* Alias list */
    ShellFunc   *functions;     /* Function list */
    History      history;
    JobTable     jobs;
    ShellOptions opts;
    Arena       *arena;         /* Per-command arena */
    int          last_status;   /* $? */
    DWORD        shell_pid;     /* $$ */
    DWORD        last_bg_pid;   /* $! */
    char         cwd[MAX_PATH]; /* Current directory */
    /* I/O handles (may be redirected) */
    HANDLE       h_stdin;
    HANDLE       h_stdout;
    HANDLE       h_stderr;
    /* Write callback for shell output to the terminal renderer */
    void       (*write_output)(const char *buf, int len, void *ud);
    void        *write_ud;
    bool         exit_requested;
    int          exit_code;
} ShellContext;

/* ─── API ────────────────────────────────────────────────────────────────── */

void shell_init(ShellContext *ctx);
void shell_free(ShellContext *ctx);

/* Execute one command line string; returns exit status */
int shell_exec_line(ShellContext *ctx, const char *line);

/* Source a file */
int shell_source(ShellContext *ctx, const char *path);

/* Output a line to the terminal (uses write_output callback) */
void shell_print(ShellContext *ctx, const char *s);
void shell_println(ShellContext *ctx, const char *s);
void shell_printf(ShellContext *ctx, const char *fmt, ...);

/* Expand prompt string (%~ %n etc.) */
char *shell_expand_prompt(ShellContext *ctx, const char *fmt);

/* Env helpers */
const char *shell_getenv(ShellContext *ctx, const char *name);
void        shell_setenv(ShellContext *ctx, const char *name, const char *value, bool export_it);
void        shell_unsetenv(ShellContext *ctx, const char *name);

/* Which utility: search PATH for executable */
char *shell_which(ShellContext *ctx, const char *name);

#endif /* WSH_SHELL_H */
