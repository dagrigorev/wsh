/*
 * parser.c — Recursive-descent parser for ZSH-compatible shell grammar.
 *
 * Grammar (simplified EBNF):
 *
 *   list        ::= and_or ( (';' | '\n' | '&') and_or )* [ ';' | '\n' | '&' ]
 *   and_or      ::= pipeline ( ('&&' | '||') NEWLINE* pipeline )*
 *   pipeline    ::= ['!'] cmd ( '|' NEWLINE* cmd )*
 *   cmd         ::= simple_cmd | compound_cmd
 *   compound_cmd::= if_cmd | while_cmd | until_cmd | for_cmd
 *                 | case_cmd | func_def | group | subshell
 *   simple_cmd  ::= ASSIGN* WORD+ redir*
 *
 * Error recovery: on a parse error, parser_parse() sets p->error and returns
 * NULL.  The caller should display p->errmsg and discard the command.
 *
 * Key decisions:
 *   - All nodes allocated in the arena → zero individual HeapAlloc calls.
 *   - Backtracking for 'name() {}' detection saves and restores lexer state.
 *   - Compound commands end with their own terminator (fi, done, esac);
 *     the list parser stops on these without consuming them.
 */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "parser.h"
#include "lexer.h"
#include "../core/log.h"

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void advance(Parser *p) { p->cur = lex_next(p->lex); }

static bool at(const Parser *p, TokenKind k) { return p->cur.kind == k; }

static bool eat(Parser *p, TokenKind k) {
    if (p->cur.kind == k) { advance(p); return true; }
    return false;
}

static void parse_error(Parser *p, const char *msg) {
    if (!p->error) {
        p->error = 1;
        _snprintf(p->errmsg, sizeof(p->errmsg),
                  "parse error (line %d): %s (got '%s')",
                  p->cur.line, msg, tok_name(p->cur.kind));
        WSH_LOG_WARN("%s", p->errmsg);
    }
}

static ASTNode *node_new(Parser *p, NodeKind kind) {
    ASTNode *n = (ASTNode *)arena_alloc(p->arena, sizeof(ASTNode));
    n->kind = kind;
    n->line = p->cur.line;
    return n;
}

/* Skip newlines (used after && || | etc.) */
static void skip_newlines(Parser *p) {
    while (at(p, TOK_NEWLINE)) advance(p);
}

/* True if current token is a list terminator (not consumed). */
static bool is_list_term(const Parser *p) {
    TokenKind k = p->cur.kind;
    return k == TOK_EOF   || k == TOK_FI    || k == TOK_ELSE  ||
           k == TOK_ELIF  || k == TOK_DONE  || k == TOK_ESAC  ||
           k == TOK_RBRACE || k == TOK_RPAREN;
}

/* ── Redirection parsing ──────────────────────────────────────────────────── */

static Redir *parse_redirs(Parser *p) {
    Redir *head = NULL, **tail = &head;
    while (!p->error) {
        TokenKind k = p->cur.kind;
        if (k != TOK_REDIR_IN  && k != TOK_REDIR_OUT &&
            k != TOK_REDIR_APPEND && k != TOK_REDIR_HEREDOC) break;

        Redir *r  = (Redir *)arena_alloc(p->arena, sizeof(Redir));
        switch (k) {
            case TOK_REDIR_IN:     r->kind = REDIR_IN;     r->fd = 0; break;
            case TOK_REDIR_OUT:    r->kind = REDIR_OUT;    r->fd = 1; break;
            case TOK_REDIR_APPEND: r->kind = REDIR_APPEND; r->fd = 1; break;
            case TOK_REDIR_HEREDOC:r->kind = REDIR_HEREDOC;r->fd = 0; break;
            default: break;
        }
        advance(p);
        if (!at(p, TOK_WORD)) { parse_error(p, "expected filename after redirection"); break; }
        r->target = p->cur.text;
        advance(p);
        *tail = r; tail = &r->next;
    }
    return head;
}

/* ── Forward declarations ─────────────────────────────────────────────────── */

static ASTNode *parse_list(Parser *p);
static ASTNode *parse_cmd(Parser *p);

/* ── Simple command ───────────────────────────────────────────────────────── */

static ASTNode *parse_simple_cmd(Parser *p) {
    ASTNode *n = node_new(p, NODE_CMD);

    /* Collect words and leading redirections */
    char  *argv_buf[256];
    int    argc   = 0;
    Redir *redirs = NULL;

    /* Leading assignments (treated as argv words for now) */
    while (at(p, TOK_ASSIGN) && argc < 255) {
        argv_buf[argc++] = p->cur.text;
        advance(p);
    }

    while ((at(p, TOK_WORD) || at(p, TOK_ASSIGN)) && argc < 255) {
        argv_buf[argc++] = p->cur.text;
        advance(p);
        /* Interspersed redirections */
        Redir *r = parse_redirs(p);
        if (r) { Redir *e = r; while (e->next) e = e->next; e->next = redirs; redirs = r; }
    }

    /* Trailing redirections */
    {
        Redir *r = parse_redirs(p);
        if (r) { Redir *e = r; while (e->next) e = e->next; e->next = redirs; redirs = r; }
    }

    n->cmd.argv  = (char **)arena_alloc(p->arena, (size_t)(argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) n->cmd.argv[i] = argv_buf[i];
    n->cmd.argv[argc] = NULL;
    n->cmd.argc  = argc;
    n->cmd.redirs = redirs;
    return n;
}

/* ── Compound commands ────────────────────────────────────────────────────── */

static ASTNode *parse_if(Parser *p) {
    ASTNode *n = node_new(p, NODE_IF);
    advance(p); /* consume 'if' */
    n->ifnode.cond = parse_list(p);
    if (!eat(p, TOK_THEN)) parse_error(p, "expected 'then'");
    n->ifnode.body = parse_list(p);

    if (at(p, TOK_ELIF)) {
        /* Recursively build nested if: elif == else { if ... fi } */
        p->cur.kind = TOK_IF; /* re-parse as if */
        n->ifnode.alt = parse_if(p);
    } else if (eat(p, TOK_ELSE)) {
        n->ifnode.alt = parse_list(p);
    }
    if (!eat(p, TOK_FI)) parse_error(p, "expected 'fi'");
    return n;
}

static ASTNode *parse_while_until(Parser *p, NodeKind kind) {
    ASTNode *n = node_new(p, kind);
    advance(p); /* consume 'while' or 'until' */
    n->loop.cond = parse_list(p);
    if (!eat(p, TOK_DO)) parse_error(p, "expected 'do'");
    n->loop.body = parse_list(p);
    if (!eat(p, TOK_DONE)) parse_error(p, "expected 'done'");
    return n;
}

static ASTNode *parse_for(Parser *p) {
    ASTNode *n = node_new(p, NODE_FOR);
    advance(p); /* consume 'for' */

    if (!at(p, TOK_WORD)) { parse_error(p, "expected variable name after 'for'"); return n; }
    n->fornode.var = p->cur.text;
    advance(p);

    /* Optional 'in word...' */
    char *words[256]; int wc = 0;
    if (eat(p, TOK_IN)) {
        while (at(p, TOK_WORD) && wc < 255) { words[wc++] = p->cur.text; advance(p); }
    }
    n->fornode.word_count = wc;
    n->fornode.words = (char **)arena_alloc(p->arena, (size_t)(wc + 1) * sizeof(char *));
    for (int i = 0; i < wc; i++) n->fornode.words[i] = words[i];
    n->fornode.words[wc] = NULL;

    /* Consume separators before do */
    while (at(p, TOK_SEMI) || at(p, TOK_NEWLINE)) advance(p);
    if (!eat(p, TOK_DO)) parse_error(p, "expected 'do'");
    n->fornode.body = parse_list(p);
    if (!eat(p, TOK_DONE)) parse_error(p, "expected 'done'");
    return n;
}

static ASTNode *parse_function(Parser *p, char *name) {
    /* Called after we've already consumed 'function name' or 'name ()' */
    ASTNode *n = node_new(p, NODE_FUNCTION);
    n->func.name = name;
    skip_newlines(p);
    if (at(p, TOK_LBRACE)) {
        advance(p);
        n->func.body = parse_list(p);
        if (!eat(p, TOK_RBRACE)) parse_error(p, "expected '}'");
    } else {
        n->func.body = parse_cmd(p);
    }
    return n;
}

static ASTNode *parse_cmd(Parser *p) {
    /* Compound commands */
    if (at(p, TOK_IF))    return parse_if(p);
    if (at(p, TOK_WHILE)) return parse_while_until(p, NODE_WHILE);
    if (at(p, TOK_UNTIL)) return parse_while_until(p, NODE_UNTIL);
    if (at(p, TOK_FOR))   return parse_for(p);

    /* function keyword: 'function name [()]' */
    if (at(p, TOK_FUNCTION)) {
        advance(p);
        char *name = at(p, TOK_WORD) ? p->cur.text : NULL;
        if (name) advance(p);
        eat(p, TOK_LPAREN); eat(p, TOK_RPAREN); /* optional () */
        return parse_function(p, name);
    }

    /* { list } */
    if (at(p, TOK_LBRACE)) {
        advance(p);
        ASTNode *n = node_new(p, NODE_GROUP);
        n->wrap.child = parse_list(p);
        if (!eat(p, TOK_RBRACE)) parse_error(p, "expected '}'");
        return n;
    }

    /* ( list ) */
    if (at(p, TOK_LPAREN)) {
        advance(p);
        ASTNode *n = node_new(p, NODE_SUBSHELL);
        n->wrap.child = parse_list(p);
        if (!eat(p, TOK_RPAREN)) parse_error(p, "expected ')'");
        return n;
    }

    /* name () { } — function without 'function' keyword.
     * Detect by saving lexer state, looking one token ahead. */
    if (at(p, TOK_WORD)) {
        /* Save state for backtrack */
        int    saved_pos  = p->lex->pos;
        int    saved_line = p->lex->line;
        Token  saved_cur  = p->cur;
        char  *fname      = p->cur.text;

        advance(p);
        if (at(p, TOK_LPAREN)) {
            advance(p);
            if (at(p, TOK_RPAREN)) {
                advance(p);
                return parse_function(p, fname);
            }
        }
        /* Not a function def — restore */
        p->lex->pos  = saved_pos;
        p->lex->line = saved_line;
        p->cur       = saved_cur;
    }

    return parse_simple_cmd(p);
}

/* ── Pipeline ─────────────────────────────────────────────────────────────── */

static ASTNode *parse_pipeline(Parser *p) {
    bool negate = eat(p, TOK_BANG);
    ASTNode *left = parse_cmd(p);

    while (at(p, TOK_PIPE) || at(p, TOK_PIPE_ERR)) {
        advance(p);
        skip_newlines(p);
        ASTNode *right = parse_cmd(p);
        ASTNode *pipe  = node_new(p, NODE_PIPE);
        pipe->binary.left  = left;
        pipe->binary.right = right;
        left = pipe;
    }

    if (negate) {
        ASTNode *neg  = node_new(p, NODE_NEGATE);
        neg->wrap.child = left;
        return neg;
    }
    return left;
}

/* ── And-Or list ──────────────────────────────────────────────────────────── */

static ASTNode *parse_and_or(Parser *p) {
    ASTNode *left = parse_pipeline(p);
    while (at(p, TOK_AND) || at(p, TOK_OR)) {
        NodeKind kind = at(p, TOK_AND) ? NODE_AND : NODE_OR;
        advance(p);
        skip_newlines(p);
        ASTNode *right = parse_pipeline(p);
        ASTNode *n     = node_new(p, kind);
        n->binary.left  = left;
        n->binary.right = right;
        left = n;
    }
    return left;
}

/* ── List ─────────────────────────────────────────────────────────────────── */

static ASTNode *parse_list(Parser *p) {
    skip_newlines(p);
    if (is_list_term(p) || p->error) return NULL;

    ASTNode *left = parse_and_or(p);
    if (!left || p->error) return NULL;

    /* Background? */
    if (at(p, TOK_BG)) {
        advance(p);
        ASTNode *bg    = node_new(p, NODE_BG);
        bg->wrap.child = left;
        left = bg;
    }

    /* Sequence */
    while ((at(p, TOK_SEMI) || at(p, TOK_NEWLINE)) && !p->error) {
        advance(p);
        skip_newlines(p);
        if (is_list_term(p)) break;
        ASTNode *right = parse_and_or(p);
        if (!right || p->error) break;

        if (at(p, TOK_BG)) {
            advance(p);
            ASTNode *bg    = node_new(p, NODE_BG);
            bg->wrap.child = right;
            right = bg;
        }

        ASTNode *seq    = node_new(p, NODE_SEQ);
        seq->binary.left  = left;
        seq->binary.right = right;
        left = seq;
    }
    return left;
}

/* ── Public entry point ───────────────────────────────────────────────────── */

void parser_init(Parser *p, Lexer *l, Arena *arena) {
    p->lex   = l;
    p->arena = arena;
    p->error = 0;
    p->errmsg[0] = '\0';
    p->cur = lex_next(l); /* prime lookahead */
}

ASTNode *parser_parse(Parser *p) {
    return parse_list(p);
}
