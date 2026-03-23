#pragma once
/*
 * lexer.h — ZSH-compatible tokeniser.
 *
 * The Lexer is a simple hand-rolled state machine rather than a table-driven
 * scanner.  This gives us precise control over quoting rules (single quotes
 * suppress all expansion, double quotes allow $VAR and $() etc.) and avoids
 * any dependency on flex/lex.
 *
 * Open/Closed: new token kinds can be added by extending TokenKind without
 * changing the Lexer struct or the token classification logic for existing
 * kinds.
 */
#ifndef WSH_LEXER_H
#define WSH_LEXER_H

#include "../core/arena.h"
#include <stdbool.h>

/* ── Token types ──────────────────────────────────────────────────────────── */

typedef enum {
    /* Values */
    TOK_WORD,       /* any unquoted / quoted word */
    TOK_ASSIGN,     /* name=value (valid identifier before '=') */

    /* Operators */
    TOK_PIPE,       /* | */
    TOK_PIPE_ERR,   /* |& */
    TOK_AND,        /* && */
    TOK_OR,         /* || */
    TOK_BG,         /* & */
    TOK_SEMI,       /* ; */
    TOK_DSEMI,      /* ;; */
    TOK_NEWLINE,    /* \n */

    /* Redirections */
    TOK_REDIR_IN,      /* < */
    TOK_REDIR_OUT,     /* > */
    TOK_REDIR_APPEND,  /* >> */
    TOK_REDIR_HEREDOC, /* << */

    /* Grouping */
    TOK_LPAREN,  /* ( */
    TOK_RPAREN,  /* ) */
    TOK_LBRACE,  /* { */
    TOK_RBRACE,  /* } */
    TOK_BANG,    /* ! */

    /* Keywords — identified by lex_next when text matches */
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_ELIF, TOK_FI,
    TOK_WHILE, TOK_UNTIL, TOK_DO, TOK_DONE,
    TOK_FOR, TOK_IN,
    TOK_CASE, TOK_ESAC,
    TOK_FUNCTION,

    TOK_EOF,
} TokenKind;

typedef struct {
    TokenKind  kind;
    char      *text;    /* arena-allocated; valid only while arena is live */
    int        line;    /* source line number (1-based) */
} Token;

/* ── Lexer state ──────────────────────────────────────────────────────────── */

typedef struct {
    const char *src;     /* NUL-terminated input (not owned) */
    int         pos;     /* byte offset into src */
    int         line;    /* current line number */
    Arena      *arena;   /* token text is allocated here */
} Lexer;

/* ── API ──────────────────────────────────────────────────────────────────── */

/* Initialise lexer over 'src' using 'arena' for token text storage. */
void  lex_init(Lexer *l, const char *src, Arena *arena);

/* Return the next token (advances state). */
Token lex_next(Lexer *l);

/* Human-readable token kind name (for error messages). */
const char *tok_name(TokenKind k);

#endif /* WSH_LEXER_H */
