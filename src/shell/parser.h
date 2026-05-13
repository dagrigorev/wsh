#pragma once
/*
 * parser.h — Recursive-descent parser producing an AST.
 *
 * Each NodeKind maps to a distinct union member (tagged union / discriminated
 * union pattern).  The AST is allocated entirely in the ShellContext arena so
 * the whole tree is freed in O(1) by arena_reset() after execution.
 *
 * Design patterns:
 *   - Composite: compound nodes (SEQ, PIPE, AND, OR) hold child ASTNode*,
 *     forming an arbitrarily deep tree.
 *   - Command (GoF): each leaf NODE_CMD is a self-contained executable unit.
 *   - Visitor: the executor in executor.c visits each node kind by switch.
 *
 * SOLID — Open/Closed:
 *   New statement types (e.g., select) are added by:
 *     1. Adding a NodeKind constant.
 *     2. Adding a union member.
 *     3. Adding a parse_ function.
 *   No existing parse functions change.
 */
#ifndef WSH_PARSER_H
#define WSH_PARSER_H

#include "lexer.h"
#include "../core/arena.h"
#include "wsh_bool.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ── Redirection kinds ────────────────────────────────────────────────────── */

typedef enum {
    REDIR_IN,       /* fd < file */
    REDIR_OUT,      /* fd > file */
    REDIR_APPEND,   /* fd >> file */
    REDIR_HEREDOC,  /* fd << WORD */
    REDIR_DUP,      /* fd>&other_fd */
} RedirKind;

typedef struct Redir {
    RedirKind    kind;
    int          fd;          /* left-hand file descriptor (0 or 1 default) */
    char        *target;      /* filename or heredoc delimiter */
    int          target_fd;   /* for REDIR_DUP */
    struct Redir *next;
} Redir;

/* ── AST node kinds ───────────────────────────────────────────────────────── */

typedef enum {
    /* Leaf nodes */
    NODE_CMD,       /* simple command with argv */
    NODE_ASSIGN,    /* standalone name=value */
    NODE_ARITH,     /* (( expr )) */

    /* Binary combinators */
    NODE_PIPE,      /* left | right */
    NODE_AND,       /* left && right */
    NODE_OR,        /* left || right */
    NODE_SEQ,       /* left ; right */

    /* Wrappers */
    NODE_BG,        /* cmd & */
    NODE_NEGATE,    /* ! cmd */
    NODE_SUBSHELL,  /* ( list ) */
    NODE_GROUP,     /* { list } */

    /* Control flow */
    NODE_IF,
    NODE_WHILE,
    NODE_UNTIL,
    NODE_FOR,
    NODE_CASE,
    NODE_FUNCTION,
} NodeKind;

/* ── AST node ─────────────────────────────────────────────────────────────── */

typedef struct CaseArm {
    char              **patterns;
    int                 pat_count;
    struct ASTNode     *body;
} CaseArm;

typedef struct ASTNode {
    NodeKind kind;
    int      line;   /* source line for error messages */

    union {
        /* NODE_CMD */
        struct {
            char  **argv;       /* expanded argv, NULL-terminated */
            int     argc;
            Redir  *redirs;     /* linked list of redirections */
        } cmd;

        /* NODE_PIPE, NODE_AND, NODE_OR, NODE_SEQ */
        struct {
            struct ASTNode *left;
            struct ASTNode *right;
        } binary;

        /* NODE_IF */
        struct {
            struct ASTNode *cond;
            struct ASTNode *body;
            struct ASTNode *alt;   /* else / elif chain; may be NULL */
        } ifnode;

        /* NODE_WHILE, NODE_UNTIL */
        struct {
            struct ASTNode *cond;
            struct ASTNode *body;
        } loop;

        /* NODE_FOR */
        struct {
            char           *var;
            char          **words;     /* word list after 'in' */
            int             word_count;
            struct ASTNode *body;
        } fornode;

        /* NODE_BG, NODE_NEGATE, NODE_SUBSHELL, NODE_GROUP */
        struct {
            struct ASTNode *child;
            Redir          *redirs;   /* for NODE_GROUP */
        } wrap;

        /* NODE_FUNCTION */
        struct {
            char           *name;
            struct ASTNode *body;
        } func;

        /* NODE_ASSIGN */
        struct {
            char *name;
            char *value;
        } assign;

        /* NODE_ARITH */
        struct {
            char *expr;
        } arith;

        /* NODE_CASE */
        struct {
            char     *word;   /* word being matched */
            int       count;  /* number of case arms */
            CaseArm  *arms;
        } casenode;
    };
} ASTNode;

/* ── Parser state ─────────────────────────────────────────────────────────── */

typedef struct {
    Lexer  *lex;
    Token   cur;     /* lookahead token */
    Arena  *arena;
    int     error;   /* non-zero if parse error occurred */
    char    errmsg[256];
} Parser;

/* ── API ──────────────────────────────────────────────────────────────────── */

/* Initialise parser from a lexer; tokens allocated into arena. */
void     parser_init(Parser *p, Lexer *l, Arena *arena);

/* Parse a complete list (sequence of pipelines).
 * Returns NULL on empty input or parse error (check p->error). */
ASTNode *parser_parse(Parser *p);


#ifdef __cplusplus
}
#endif

#endif /* WSH_PARSER_H */
