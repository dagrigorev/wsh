#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "shell.h"
#include "builtins.h"
#include "expand.h"
#include "util.h"

/* ══════════════════════════════════════════════════════════════════════════
   ENVIRONMENT
   ══════════════════════════════════════════════════════════════════════════ */

static EnvScope *env_scope_new(EnvScope *parent) {
    EnvScope *s = (EnvScope *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EnvScope));
    if (s) s->parent = parent;
    return s;
}

static void env_scope_free(EnvScope *s) {
    if (!s) return;
    for (EnvVar *v = s->vars; v;) {
        EnvVar *next = v->next;
        HeapFree(GetProcessHeap(), 0, v->name);
        HeapFree(GetProcessHeap(), 0, v->value);
        HeapFree(GetProcessHeap(), 0, v);
        v = next;
    }
    HeapFree(GetProcessHeap(), 0, s);
}

const char *shell_getenv(ShellContext *ctx, const char *name) {
    if (!ctx || !name) return NULL;
    /* Walk scope chain */
    for (EnvScope *s = ctx->env; s; s = s->parent) {
        for (EnvVar *v = s->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) return v->value;
        }
    }
    /* Fall back to process environment */
    static char envbuf[4096];
    DWORD n = GetEnvironmentVariableA(name, envbuf, sizeof(envbuf));
    return n ? envbuf : NULL;
}

void shell_setenv(ShellContext *ctx, const char *name, const char *value, bool export_it) {
    if (!ctx || !name) return;
    EnvScope *s = ctx->env;
    /* Find existing in top scope */
    for (EnvVar *v = s->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            HeapFree(GetProcessHeap(), 0, v->value);
            v->value    = str_dup(value ? value : "");
            if (export_it) v->exported = true;
            if (v->exported) SetEnvironmentVariableA(name, v->value);
            return;
        }
    }
    /* Create new */
    EnvVar *v = (EnvVar *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EnvVar));
    v->name     = str_dup(name);
    v->value    = str_dup(value ? value : "");
    v->exported = export_it;
    v->next     = s->vars;
    s->vars     = v;
    if (export_it) SetEnvironmentVariableA(name, v->value);
}

void shell_unsetenv(ShellContext *ctx, const char *name) {
    if (!ctx || !name) return;
    for (EnvScope *s = ctx->env; s; s = s->parent) {
        EnvVar **pp = &s->vars;
        while (*pp) {
            if (strcmp((*pp)->name, name) == 0) {
                EnvVar *dead = *pp;
                *pp = dead->next;
                if (dead->exported) SetEnvironmentVariableA(name, NULL);
                HeapFree(GetProcessHeap(), 0, dead->name);
                HeapFree(GetProcessHeap(), 0, dead->value);
                HeapFree(GetProcessHeap(), 0, dead);
                return;
            }
            pp = &(*pp)->next;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   INIT / FREE
   ══════════════════════════════════════════════════════════════════════════ */

void shell_init(ShellContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->env       = env_scope_new(NULL);
    ctx->arena     = arena_create(65536);
    ctx->shell_pid = GetCurrentProcessId();
    ctx->h_stdin   = GetStdHandle(STD_INPUT_HANDLE);
    ctx->h_stdout  = GetStdHandle(STD_OUTPUT_HANDLE);
    ctx->h_stderr  = GetStdHandle(STD_ERROR_HANDLE);

    /* Get initial working directory */
    wchar_t wcwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, wcwd);
    char *cwd = utf16_to_utf8(wcwd, NULL);
    if (cwd) { strncpy(ctx->cwd, cwd, MAX_PATH-1); HeapFree(GetProcessHeap(), 0, cwd); }

    /* Sync HOME from USERPROFILE if not set */
    if (!GetEnvironmentVariableA("HOME", NULL, 0)) {
        char profile[MAX_PATH] = {0};
        GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
        if (profile[0]) SetEnvironmentVariableA("HOME", profile);
    }

    shell_setenv(ctx, "PWD",   ctx->cwd, true);
    shell_setenv(ctx, "SHELL", "wsh",    true);

    /* IFS default */
    shell_setenv(ctx, "IFS", " \t\n", false);

    history_init(&ctx->history);
    job_table_init(&ctx->jobs);
    ctx->opts.auto_wrap     = false;
    ctx->opts.interactive   = true;
}

void shell_free(ShellContext *ctx) {
    history_free(&ctx->history);
    job_table_free(&ctx->jobs);

    /* Free aliases */
    for (Alias *a = ctx->aliases; a;) {
        Alias *next = a->next;
        HeapFree(GetProcessHeap(), 0, a->name);
        HeapFree(GetProcessHeap(), 0, a->value);
        HeapFree(GetProcessHeap(), 0, a);
        a = next;
    }

    /* Free functions */
    for (ShellFunc *f = ctx->functions; f;) {
        ShellFunc *next = f->next;
        HeapFree(GetProcessHeap(), 0, f->name);
        HeapFree(GetProcessHeap(), 0, f);
        f = next;
    }

    /* Free env scopes */
    EnvScope *s = ctx->env;
    while (s) {
        EnvScope *parent = s->parent;
        env_scope_free(s);
        s = parent;
    }

    if (ctx->arena) arena_destroy(ctx->arena);
}

/* ══════════════════════════════════════════════════════════════════════════
   OUTPUT
   ══════════════════════════════════════════════════════════════════════════ */

void shell_print(ShellContext *ctx, const char *s) {
    if (!s) return;
    if (ctx->write_output) ctx->write_output(s, (int)strlen(s), ctx->write_ud);
    else { DWORD w; WriteFile(ctx->h_stdout, s, (DWORD)strlen(s), &w, NULL); }
}

void shell_println(ShellContext *ctx, const char *s) {
    shell_print(ctx, s); shell_print(ctx, "\r\n");
}

void shell_printf(ShellContext *ctx, const char *fmt, ...) {
    char buf[4096]; va_list ap;
    va_start(ap, fmt); _vsnprintf(buf, sizeof(buf)-1, fmt, ap); va_end(ap);
    shell_print(ctx, buf);
}

/* ══════════════════════════════════════════════════════════════════════════
   WHICH
   ══════════════════════════════════════════════════════════════════════════ */

char *shell_which(ShellContext *ctx, const char *name) {
    if (!name) return NULL;
    static const char *EXT[] = { "", ".exe", ".cmd", ".bat", NULL };

    /* Absolute path */
    if (name[0] == '\\' || name[0] == '/' || (name[1] == ':')) {
        for (int e = 0; EXT[e]; e++) {
            char full[MAX_PATH]; _snprintf(full, MAX_PATH, "%s%s", name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) return str_dup(full);
        }
        return NULL;
    }

    /* Search PATH */
    char pathenv[32768] = {0};
    GetEnvironmentVariableA("PATH", pathenv, sizeof(pathenv));
    char **dirs = NULL;
    int ndir = str_split(pathenv, ';', &dirs);
    char *result = NULL;

    for (int d = 0; d < ndir && !result; d++) {
        if (!dirs[d] || !dirs[d][0]) continue;
        for (int e = 0; EXT[e] && !result; e++) {
            char full[MAX_PATH];
            _snprintf(full, MAX_PATH, "%s\\%s%s", dirs[d], name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) result = str_dup(full);
        }
    }
    str_split_free(dirs, ndir);
    (void)ctx;
    return result;
}

/* ══════════════════════════════════════════════════════════════════════════
   TOKENIZER
   ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *src;
    int         pos;
    int         lineno;
    Arena      *arena;
} Lexer;

static int peek(Lexer *l) { return (unsigned char)l->src[l->pos]; }
static int next_ch(Lexer *l) {
    int c = (unsigned char)l->src[l->pos];
    if (c) l->pos++;
    if (c == '\n') l->lineno++;
    return c;
}

/* Read a word token including all quoting forms */
static char *read_word(Lexer *l) {
    /* Dynamic buffer */
    char buf[8192]; int bi = 0;
    bool done = false;
    while (!done && peek(l)) {
        int c = peek(l);
        switch (c) {
            case '\'': {
                next_ch(l);
                while (peek(l) && peek(l) != '\'') buf[bi++] = (char)next_ch(l);
                if (peek(l) == '\'') next_ch(l);
                break;
            }
            case '"': {
                next_ch(l);
                while (peek(l) && peek(l) != '"') {
                    if (peek(l) == '\\' && l->src[l->pos+1]) {
                        next_ch(l); buf[bi++] = (char)next_ch(l);
                    } else {
                        buf[bi++] = (char)next_ch(l);
                    }
                }
                if (peek(l) == '"') next_ch(l);
                break;
            }
            case '\\':
                next_ch(l);
                if (peek(l) == '\n') { next_ch(l); /* line continuation */ }
                else if (peek(l)) buf[bi++] = (char)next_ch(l);
                break;
            case ' ': case '\t': case '\r': case '\n':
            case ';': case '|': case '&': case '<': case '>':
            case '(': case ')': case '{': case '}':
                done = true; break;
            default:
                buf[bi++] = (char)next_ch(l);
                break;
        }
        if (bi >= (int)sizeof(buf) - 4) break;
    }
    buf[bi] = '\0';
    return arena_strdup(l->arena, buf);
}

static void skip_whitespace(Lexer *l) {
    while (peek(l) == ' ' || peek(l) == '\t' || peek(l) == '\r') next_ch(l);
}

static Token lex_next(Lexer *l) {
    Token t = {0};
    t.lineno = l->lineno;

    skip_whitespace(l);
    int c = peek(l);

    if (!c) { t.kind = TOK_EOF; return t; }

    /* Comments */
    if (c == '#') {
        while (peek(l) && peek(l) != '\n') next_ch(l);
        return lex_next(l);
    }

    switch (c) {
        case '\n': next_ch(l); t.kind = TOK_NEWLINE; return t;
        case ';':
            next_ch(l);
            if (peek(l) == ';') { next_ch(l); t.kind = TOK_DSEMI; }
            else t.kind = TOK_SEMI;
            return t;
        case '&':
            next_ch(l);
            if (peek(l) == '&') { next_ch(l); t.kind = TOK_AND; }
            else t.kind = TOK_BG;
            return t;
        case '|':
            next_ch(l);
            if (peek(l) == '|') { next_ch(l); t.kind = TOK_OR; }
            else if (peek(l) == '&') { next_ch(l); t.kind = TOK_PIPE_ERR; }
            else t.kind = TOK_PIPE;
            return t;
        case '<':
            next_ch(l);
            if (peek(l) == '<') { next_ch(l); t.kind = TOK_REDIR_HEREDOC; }
            else t.kind = TOK_REDIR_IN;
            return t;
        case '>':
            next_ch(l);
            if (peek(l) == '>') { next_ch(l); t.kind = TOK_REDIR_APPEND; }
            else t.kind = TOK_REDIR_OUT;
            return t;
        case '(': next_ch(l); t.kind = TOK_LPAREN; return t;
        case ')': next_ch(l); t.kind = TOK_RPAREN; return t;
        case '{': next_ch(l); t.kind = TOK_LBRACE; return t;
        case '}': next_ch(l); t.kind = TOK_RBRACE; return t;
        case '!': next_ch(l); t.kind = TOK_BANG;   return t;
        default:
            t.text = read_word(l);
            /* Classify keywords */
            if      (!strcmp(t.text, "if"))       t.kind = TOK_IF;
            else if (!strcmp(t.text, "then"))      t.kind = TOK_THEN;
            else if (!strcmp(t.text, "else"))      t.kind = TOK_ELSE;
            else if (!strcmp(t.text, "elif"))      t.kind = TOK_ELIF;
            else if (!strcmp(t.text, "fi"))        t.kind = TOK_FI;
            else if (!strcmp(t.text, "while"))     t.kind = TOK_WHILE;
            else if (!strcmp(t.text, "until"))     t.kind = TOK_WHILE; /* reuse */
            else if (!strcmp(t.text, "do"))        t.kind = TOK_DO;
            else if (!strcmp(t.text, "done"))      t.kind = TOK_DONE;
            else if (!strcmp(t.text, "for"))       t.kind = TOK_FOR;
            else if (!strcmp(t.text, "in"))        t.kind = TOK_IN;
            else if (!strcmp(t.text, "case"))      t.kind = TOK_CASE;
            else if (!strcmp(t.text, "esac"))      t.kind = TOK_ESAC;
            else if (!strcmp(t.text, "function"))  t.kind = TOK_FUNCTION;
            else {
                /* Check for ASSIGN: name=value */
                const char *eq = strchr(t.text, '=');
                if (eq && eq != t.text) {
                    bool valid_name = true;
                    for (const char *p = t.text; p != eq; p++) {
                        if (!isalnum((unsigned char)*p) && *p != '_') { valid_name = false; break; }
                    }
                    if (valid_name) t.kind = TOK_ASSIGN;
                    else t.kind = TOK_WORD;
                } else {
                    t.kind = TOK_WORD;
                }
            }
            return t;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   PARSER
   ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    Lexer  *lex;
    Token   cur;
    Arena  *arena;
} Parser;

static void parser_advance(Parser *p) { p->cur = lex_next(p->lex); }

static ASTNode *node_new(Parser *p, NodeKind kind) {
    ASTNode *n = (ASTNode *)arena_alloc(p->arena, sizeof(ASTNode));
    if (n) memset(n, 0, sizeof(*n));
    if (n) n->kind = kind;
    return n;
}

static ASTNode *parse_cmd(Parser *p);
static ASTNode *parse_pipeline(Parser *p);
static ASTNode *parse_andor(Parser *p);
static ASTNode *parse_list(Parser *p);
static ASTNode *parse_compound(Parser *p);

/* Parse redirections attached to a command */
static Redir *parse_redirs(Parser *p) {
    Redir *head = NULL, **tail = &head;
    while (p->cur.kind == TOK_REDIR_IN || p->cur.kind == TOK_REDIR_OUT ||
           p->cur.kind == TOK_REDIR_APPEND || p->cur.kind == TOK_REDIR_HEREDOC) {
        Redir *r = (Redir *)arena_alloc(p->arena, sizeof(Redir));
        memset(r, 0, sizeof(*r));
        switch (p->cur.kind) {
            case TOK_REDIR_IN:     r->kind = REDIR_IN;     r->fd = 0; break;
            case TOK_REDIR_OUT:    r->kind = REDIR_OUT;    r->fd = 1; break;
            case TOK_REDIR_APPEND: r->kind = REDIR_APPEND; r->fd = 1; break;
            case TOK_REDIR_HEREDOC:r->kind = REDIR_HEREDOC;r->fd = 0; break;
            default: break;
        }
        parser_advance(p);
        if (p->cur.kind == TOK_WORD) {
            r->target = p->cur.text;
            parser_advance(p);
        }
        *tail = r; tail = &r->next;
    }
    return head;
}

static ASTNode *parse_simple_cmd(Parser *p) {
    ASTNode *n = node_new(p, NODE_CMD);
    char *argv_buf[256]; int argc = 0;
    Redir *redirs = NULL;

    /* Leading assignments */
    while (p->cur.kind == TOK_ASSIGN) {
        /* Treat as words for now */
        if (argc < 255) argv_buf[argc++] = p->cur.text;
        parser_advance(p);
    }

    while (p->cur.kind == TOK_WORD || p->cur.kind == TOK_ASSIGN) {
        if (argc < 255) argv_buf[argc++] = p->cur.text;
        parser_advance(p);
        Redir *r = parse_redirs(p);
        if (r) { Redir *end = r; while (end->next) end = end->next; end->next = redirs; redirs = r; }
    }
    Redir *r2 = parse_redirs(p);
    if (r2) { Redir *end = r2; while (end->next) end = end->next; end->next = redirs; redirs = r2; }

    n->cmd.argc  = argc;
    n->cmd.argv  = (char **)arena_alloc(p->arena, (size_t)(argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) n->cmd.argv[i] = argv_buf[i];
    n->cmd.argv[argc] = NULL;
    n->cmd.redirs = redirs;
    return n;
}

static ASTNode *parse_compound(Parser *p) {
    /* if ... then ... [elif ... then] ... [else ...] fi */
    if (p->cur.kind == TOK_IF) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_IF);
        n->ifnode.cond = parse_list(p);
        if (p->cur.kind == TOK_THEN) parser_advance(p);
        n->ifnode.body = parse_list(p);
        if (p->cur.kind == TOK_ELIF) {
            /* Recursively build nested if */
            p->cur.kind = TOK_IF; /* re-parse as if */
            n->ifnode.alt = parse_compound(p);
        } else if (p->cur.kind == TOK_ELSE) {
            parser_advance(p);
            n->ifnode.alt = parse_list(p);
        }
        if (p->cur.kind == TOK_FI) parser_advance(p);
        return n;
    }
    /* while / until */
    if (p->cur.kind == TOK_WHILE) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_WHILE);
        n->whilenode.cond = parse_list(p);
        if (p->cur.kind == TOK_DO) parser_advance(p);
        n->whilenode.body = parse_list(p);
        if (p->cur.kind == TOK_DONE) parser_advance(p);
        return n;
    }
    /* for var in words; do body; done */
    if (p->cur.kind == TOK_FOR) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_FOR);
        if (p->cur.kind == TOK_WORD) { n->fornode.var = p->cur.text; parser_advance(p); }
        if (p->cur.kind == TOK_IN) {
            parser_advance(p);
            char *words[256]; int wc = 0;
            while (p->cur.kind == TOK_WORD && wc < 255) {
                words[wc++] = p->cur.text; parser_advance(p);
            }
            n->fornode.word_count = wc;
            n->fornode.words = (char **)arena_alloc(p->arena, (size_t)(wc+1)*sizeof(char*));
            for (int i = 0; i < wc; i++) n->fornode.words[i] = words[i];
            n->fornode.words[wc] = NULL;
        }
        while (p->cur.kind == TOK_SEMI || p->cur.kind == TOK_NEWLINE) parser_advance(p);
        if (p->cur.kind == TOK_DO) parser_advance(p);
        n->fornode.body = parse_list(p);
        if (p->cur.kind == TOK_DONE) parser_advance(p);
        return n;
    }
    /* function name() { body } */
    if (p->cur.kind == TOK_FUNCTION) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_FUNCTION);
        if (p->cur.kind == TOK_WORD) { n->func.name = p->cur.text; parser_advance(p); }
        /* Consume optional () */
        if (p->cur.kind == TOK_LPAREN) { parser_advance(p); if (p->cur.kind == TOK_RPAREN) parser_advance(p); }
        while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
        if (p->cur.kind == TOK_LBRACE) {
            parser_advance(p);
            n->func.body = parse_list(p);
            if (p->cur.kind == TOK_RBRACE) parser_advance(p);
        }
        return n;
    }
    /* name() { body } — function without 'function' keyword.
       Detected by parse_cmd after peeking at next token (LPAREN). */
    /* { list } — command group */
    if (p->cur.kind == TOK_LBRACE) {
        parser_advance(p);
        ASTNode *inner = parse_list(p);
        if (p->cur.kind == TOK_RBRACE) parser_advance(p);
        return inner;
    }
    /* ( list ) — subshell */
    if (p->cur.kind == TOK_LPAREN) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_SUBSHELL);
        n->wrap.child = parse_list(p);
        if (p->cur.kind == TOK_RPAREN) parser_advance(p);
        return n;
    }
    return parse_simple_cmd(p);
}

static ASTNode *parse_cmd(Parser *p) {
    /* Check for compound commands */
    if (p->cur.kind == TOK_IF || p->cur.kind == TOK_WHILE ||
        p->cur.kind == TOK_FOR || p->cur.kind == TOK_FUNCTION ||
        p->cur.kind == TOK_LBRACE || p->cur.kind == TOK_LPAREN) {
        return parse_compound(p);
    }
    /* name() { } without 'function' keyword — detect via lookahead */
    if (p->cur.kind == TOK_WORD) {
        int saved_pos    = p->lex->pos;
        int saved_lineno = p->lex->lineno;
        Token saved_cur  = p->cur;
        char *fname      = p->cur.text;
        parser_advance(p);
        if (p->cur.kind == TOK_LPAREN) {
            parser_advance(p);
            if (p->cur.kind == TOK_RPAREN) {
                parser_advance(p);
                while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
                ASTNode *n = node_new(p, NODE_FUNCTION);
                n->func.name = fname;
                if (p->cur.kind == TOK_LBRACE) {
                    parser_advance(p);
                    n->func.body = parse_list(p);
                    if (p->cur.kind == TOK_RBRACE) parser_advance(p);
                }
                return n;
            }
        }
        p->lex->pos    = saved_pos;
        p->lex->lineno = saved_lineno;
        p->cur         = saved_cur;
    }
    return parse_simple_cmd(p);
}

static ASTNode *parse_pipeline(Parser *p) {
    bool negate = false;
    if (p->cur.kind == TOK_BANG) { negate = true; parser_advance(p); }
    ASTNode *left = parse_cmd(p);
    while (p->cur.kind == TOK_PIPE || p->cur.kind == TOK_PIPE_ERR) {
        parser_advance(p);
        while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
        ASTNode *right = parse_cmd(p);
        ASTNode *pipe_node = node_new(p, NODE_PIPE);
        pipe_node->binary.left  = left;
        pipe_node->binary.right = right;
        left = pipe_node;
    }
    (void)negate; /* TODO: wrap in negation node */
    return left;
}

static ASTNode *parse_andor(Parser *p) {
    ASTNode *left = parse_pipeline(p);
    while (p->cur.kind == TOK_AND || p->cur.kind == TOK_OR) {
        TokenKind op = p->cur.kind;
        parser_advance(p);
        while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
        ASTNode *right = parse_pipeline(p);
        ASTNode *n = node_new(p, op == TOK_AND ? NODE_AND : NODE_OR);
        n->binary.left  = left;
        n->binary.right = right;
        left = n;
    }
    return left;
}

static ASTNode *parse_list(Parser *p) {
    while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
    if (p->cur.kind == TOK_EOF || p->cur.kind == TOK_THEN ||
        p->cur.kind == TOK_ELSE || p->cur.kind == TOK_ELIF ||
        p->cur.kind == TOK_FI   || p->cur.kind == TOK_DO  ||
        p->cur.kind == TOK_DONE || p->cur.kind == TOK_ESAC ||
        p->cur.kind == TOK_RBRACE || p->cur.kind == TOK_RPAREN) {
        return NULL;
    }

    ASTNode *left = parse_andor(p);
    if (!left) return NULL;

    /* & — background */
    if (p->cur.kind == TOK_BG) {
        parser_advance(p);
        ASTNode *n = node_new(p, NODE_BG);
        n->wrap.child = left;
        left = n;
    }

    /* ; or newline — sequence */
    while (p->cur.kind == TOK_SEMI || p->cur.kind == TOK_NEWLINE) {
        parser_advance(p);
        while (p->cur.kind == TOK_NEWLINE) parser_advance(p);
        if (p->cur.kind == TOK_EOF || p->cur.kind == TOK_THEN ||
            p->cur.kind == TOK_ELSE || p->cur.kind == TOK_ELIF ||
            p->cur.kind == TOK_FI   || p->cur.kind == TOK_DO  ||
            p->cur.kind == TOK_DONE || p->cur.kind == TOK_ESAC ||
            p->cur.kind == TOK_RBRACE || p->cur.kind == TOK_RPAREN) break;
        ASTNode *right = parse_andor(p);
        if (!right) break;
        if (p->cur.kind == TOK_BG) {
            parser_advance(p);
            ASTNode *bg = node_new(p, NODE_BG);
            bg->wrap.child = right;
            right = bg;
        }
        ASTNode *seq = node_new(p, NODE_SEQ);
        seq->binary.left  = left;
        seq->binary.right = right;
        left = seq;
    }
    return left;
}

/* ══════════════════════════════════════════════════════════════════════════
   EXECUTOR
   ══════════════════════════════════════════════════════════════════════════ */

static int exec_node(ShellContext *ctx, ASTNode *node);

/* Apply redirections; save old handles; returns true on success */
static bool apply_redirs(ShellContext *ctx, Redir *redirs,
                          HANDLE *saved_in, HANDLE *saved_out, HANDLE *saved_err) {
    *saved_in  = ctx->h_stdin;
    *saved_out = ctx->h_stdout;
    *saved_err = ctx->h_stderr;

    for (Redir *r = redirs; r; r = r->next) {
        char *target = r->target ? expand_string(ctx, r->target) : NULL;
        switch (r->kind) {
            case REDIR_IN: {
                if (!target) continue;
                wchar_t *wt = utf8_to_utf16(target, NULL);
                HANDLE h = CreateFileW(wt, GENERIC_READ, FILE_SHARE_READ,
                                       NULL, OPEN_EXISTING, 0, NULL);
                if (wt) HeapFree(GetProcessHeap(), 0, wt);
                if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(),0,target); return false; }
                ctx->h_stdin = h;
                break;
            }
            case REDIR_OUT:
            case REDIR_APPEND: {
                if (!target) continue;
                wchar_t *wt = utf8_to_utf16(target, NULL);
                DWORD create = (r->kind == REDIR_APPEND) ? OPEN_ALWAYS : CREATE_ALWAYS;
                HANDLE h = CreateFileW(wt, GENERIC_WRITE, FILE_SHARE_READ,
                                       NULL, create, 0, NULL);
                if (wt) HeapFree(GetProcessHeap(), 0, wt);
                if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(),0,target); return false; }
                if (r->kind == REDIR_APPEND) SetFilePointer(h, 0, NULL, FILE_END);
                if (r->fd == 2) ctx->h_stderr = h;
                else             ctx->h_stdout = h;
                break;
            }
            default: break;
        }
        if (target) HeapFree(GetProcessHeap(), 0, target);
    }
    return true;
}

static void restore_redirs(ShellContext *ctx,
                            HANDLE saved_in, HANDLE saved_out, HANDLE saved_err) {
    if (ctx->h_stdin  != saved_in)  { CloseHandle(ctx->h_stdin);  ctx->h_stdin  = saved_in; }
    if (ctx->h_stdout != saved_out) { CloseHandle(ctx->h_stdout); ctx->h_stdout = saved_out; }
    if (ctx->h_stderr != saved_err) { CloseHandle(ctx->h_stderr); ctx->h_stderr = saved_err; }
}

/* Spawn an external process, connecting it to current ctx I/O handles */
static int spawn_external(ShellContext *ctx, char **argv, int argc,
                           bool background, DWORD *out_pid) {
    if (argc < 1 || !argv[0]) return 127;

    char *exe = shell_which(ctx, argv[0]);
    if (!exe) {
        shell_printf(ctx, "wsh: %s: command not found\r\n", argv[0]);
        return 127;
    }

    /* Build command line string */
    char cmdline[32768]; int ci = 0;
    for (int i = 0; i < argc; i++) {
        if (i) cmdline[ci++] = ' ';
        bool need_quote = strchr(argv[i], ' ') || strchr(argv[i], '\t');
        if (need_quote) cmdline[ci++] = '"';
        int l = (int)strlen(argv[i]);
        if (ci + l < (int)sizeof(cmdline) - 4) { memcpy(cmdline + ci, argv[i], l); ci += l; }
        if (need_quote) cmdline[ci++] = '"';
    }
    cmdline[ci] = '\0';
    HeapFree(GetProcessHeap(), 0, exe);

    wchar_t *wcmd = utf8_to_utf16(cmdline, NULL);
    if (!wcmd) return 1;

    /* Inherit the shell's redirected handles */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOW si = {0}; si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = ctx->h_stdin;
    si.hStdOutput = ctx->h_stdout;
    si.hStdError  = ctx->h_stderr;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE,
                              CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi);
    HeapFree(GetProcessHeap(), 0, wcmd);

    if (!ok) {
        wsh_log_win32_error("CreateProcessW (external)");
        shell_printf(ctx, "wsh: %s: execution failed\r\n", argv[0]);
        return 126;
    }

    CloseHandle(pi.hThread);

    if (out_pid) *out_pid = pi.dwProcessId;

    if (background) {
        job_add(&ctx->jobs, pi.dwProcessId, pi.hProcess, cmdline);
        ctx->last_bg_pid = pi.dwProcessId;
        shell_printf(ctx, "[%d] %lu\r\n",
                     ctx->jobs.count, (unsigned long)pi.dwProcessId);
        return 0;
    }

    /* Foreground: wait */
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* Execute NODE_CMD */
static int exec_cmd(ShellContext *ctx, ASTNode *node, bool background) {
    if (!node || node->cmd.argc == 0) return 0;

    /* Apply redirections */
    HANDLE si, so, se;
    if (!apply_redirs(ctx, node->cmd.redirs, &si, &so, &se)) return 1;

    /* Expand all argv */
    char *eargv[1024]; int eargc = 0;
    for (int i = 0; i < node->cmd.argc && eargc < 1023; i++) {
        WordList wl = expand_word(ctx, node->cmd.argv[i], true, true);
        for (int j = 0; j < wl.count && eargc < 1023; j++) {
            eargv[eargc++] = wl.words[j];
            wl.words[j] = NULL; /* transfer ownership */
        }
        wordlist_free(&wl);
    }
    eargv[eargc] = NULL;

    /* xtrace */
    if (ctx->opts.xtrace) {
        shell_print(ctx, "+ ");
        for (int i = 0; i < eargc; i++) {
            if (i) shell_print(ctx, " ");
            shell_print(ctx, eargv[i]);
        }
        shell_print(ctx, "\r\n");
    }

    int ret = 0;

    /* Alias expansion of argv[0] */
    if (eargc > 0) {
        for (Alias *a = ctx->aliases; a; a = a->next) {
            if (strcmp(a->name, eargv[0]) == 0) {
                /* Re-execute with alias value prepended */
                char expanded[8192];
                _snprintf(expanded, sizeof(expanded), "%s %s", a->value,
                          eargc > 1 ? eargv[1] : "");
                ret = shell_exec_line(ctx, expanded);
                goto cleanup;
            }
        }
    }

    /* Built-in */
    if (eargc > 0) {
        BuiltinFn fn = builtin_find(eargv[0]);
        if (fn) { ret = fn(eargc, eargv, ctx); goto cleanup; }
    }

    /* Shell function */
    if (eargc > 0) {
        for (ShellFunc *f = ctx->functions; f; f = f->next) {
            if (strcmp(f->name, eargv[0]) == 0) {
                /* Push new scope, bind $@ $# positionals */
                EnvScope *old = ctx->env;
                ctx->env = env_scope_new(old);
                /* Set $1 .. $N */
                for (int i = 1; i < eargc; i++) {
                    char idx[8]; _snprintf(idx, sizeof(idx), "%d", i);
                    shell_setenv(ctx, idx, eargv[i], false);
                }
                ret = exec_node(ctx, f->body);
                env_scope_free(ctx->env);
                ctx->env = old;
                goto cleanup;
            }
        }
    }

    /* External command */
    DWORD ext_pid = 0;
    ret = spawn_external(ctx, eargv, eargc, background, &ext_pid);

cleanup:
    for (int i = 0; i < eargc; i++) if (eargv[i]) HeapFree(GetProcessHeap(), 0, eargv[i]);
    restore_redirs(ctx, si, so, se);
    return ret;
}

/* Execute pipe: left | right */
static int exec_pipe(ShellContext *ctx, ASTNode *node) {
    HANDLE hread, hwrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hread, &hwrite, &sa, 0)) return 1;

    /* Left side writes to hwrite */
    HANDLE old_out = ctx->h_stdout;
    ctx->h_stdout = hwrite;
    /* Run left in background thread conceptually — simplified: run left, then right */
    int left_ret = exec_node(ctx, node->binary.left);
    ctx->h_stdout = old_out;
    CloseHandle(hwrite);

    /* Right side reads from hread */
    HANDLE old_in = ctx->h_stdin;
    ctx->h_stdin = hread;
    int right_ret = exec_node(ctx, node->binary.right);
    ctx->h_stdin = old_in;
    CloseHandle(hread);

    (void)left_ret;
    return right_ret;
}

/* Main execute dispatcher */
static int exec_node(ShellContext *ctx, ASTNode *node) {
    if (!node) return 0;
    if (ctx->exit_requested) return ctx->exit_code;

    int ret = 0;
    switch (node->kind) {
        case NODE_CMD:
            ret = exec_cmd(ctx, node, false);
            break;
        case NODE_PIPE:
            ret = exec_pipe(ctx, node);
            break;
        case NODE_AND:
            ret = exec_node(ctx, node->binary.left);
            if (ret == 0) ret = exec_node(ctx, node->binary.right);
            break;
        case NODE_OR:
            ret = exec_node(ctx, node->binary.left);
            if (ret != 0) ret = exec_node(ctx, node->binary.right);
            break;
        case NODE_SEQ:
            ret = exec_node(ctx, node->binary.left);
            if (!ctx->exit_requested) ret = exec_node(ctx, node->binary.right);
            break;
        case NODE_BG:
            if (node->wrap.child && node->wrap.child->kind == NODE_CMD) {
                /* Mark as background */
                ret = exec_cmd(ctx, node->wrap.child, true);
            } else {
                ret = exec_node(ctx, node->wrap.child);
            }
            break;
        case NODE_SUBSHELL:
            /* For simplicity: execute in same context (full fork not available on Win32 easily) */
            ret = exec_node(ctx, node->wrap.child);
            break;
        case NODE_IF:
            ret = exec_node(ctx, node->ifnode.cond);
            if (ret == 0) ret = exec_node(ctx, node->ifnode.body);
            else          ret = exec_node(ctx, node->ifnode.alt);
            break;
        case NODE_WHILE:
            while (!ctx->exit_requested) {
                ret = exec_node(ctx, node->whilenode.cond);
                if (ret != 0) break;
                ret = exec_node(ctx, node->whilenode.body);
            }
            break;
        case NODE_FOR:
            for (int i = 0; i < node->fornode.word_count && !ctx->exit_requested; i++) {
                char *val = expand_string(ctx, node->fornode.words[i]);
                shell_setenv(ctx, node->fornode.var, val, false);
                HeapFree(GetProcessHeap(), 0, val);
                ret = exec_node(ctx, node->fornode.body);
            }
            break;
        case NODE_FUNCTION: {
            /* Register function */
            ShellFunc *f = (ShellFunc *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ShellFunc));
            f->name = str_dup(node->func.name);
            f->body = node->func.body;
            f->next = ctx->functions;
            ctx->functions = f;
            ret = 0;
            break;
        }
        case NODE_ASSIGN: {
            char *val = expand_string(ctx, node->assign.value);
            shell_setenv(ctx, node->assign.name, val, false);
            HeapFree(GetProcessHeap(), 0, val);
            ret = 0;
            break;
        }
        case NODE_ARITH:
            expand_arith(ctx, node->arith.expr);
            ret = 0;
            break;
        default:
            ret = 0;
            break;
    }

    ctx->last_status = ret;
    if (ctx->opts.err_exit && ret != 0) ctx->exit_requested = true;
    return ret;
}

/* ══════════════════════════════════════════════════════════════════════════
   PUBLIC: shell_exec_line
   ══════════════════════════════════════════════════════════════════════════ */

int shell_exec_line(ShellContext *ctx, const char *line) {
    if (!line || !line[0]) return 0;

    /* History expansion */
    char expanded_line[8192];
    if (line[0] == '!') {
        if (!history_expand(&ctx->history, line, expanded_line, sizeof(expanded_line)))
            strncpy(expanded_line, line, sizeof(expanded_line)-1);
        line = expanded_line;
    }

    /* AUTO_CD: if line is a valid directory, cd into it */
    if (ctx->opts.auto_cd) {
        char *exp = expand_tilde(ctx, line);
        if (path_is_dir(exp)) {
            char *argv2[2] = { "cd", exp };
            int r = builtin_cd(2, argv2, ctx);
            HeapFree(GetProcessHeap(), 0, exp);
            return r;
        }
        HeapFree(GetProcessHeap(), 0, exp);
    }

    arena_reset(ctx->arena);

    Lexer lex;
    lex.src    = line;
    lex.pos    = 0;
    lex.lineno = 1;
    lex.arena  = ctx->arena;

    Parser parser;
    parser.lex   = &lex;
    parser.arena = ctx->arena;
    parser_advance(&parser);

    ASTNode *ast = parse_list(&parser);
    if (!ast) return 0;

    int ret = exec_node(ctx, ast);
    ctx->last_status = ret;
    return ret;
}

/* ══════════════════════════════════════════════════════════════════════════
   source a file
   ══════════════════════════════════════════════════════════════════════════ */

int shell_source(ShellContext *ctx, const char *path) {
    wchar_t *wp = utf8_to_utf16(path, NULL);
    FILE *f = NULL;
    if (wp) {
        f = _wfopen(wp, L"r");
        HeapFree(GetProcessHeap(), 0, wp);
    }
    if (!f) {
        shell_printf(ctx, "source: %s: No such file or directory\r\n", path);
        return 1;
    }
    char line[8192];
    int ret = 0;
    while (fgets(line, sizeof(line), f) && !ctx->exit_requested) {
        str_trim(line);
        if (line[0] && line[0] != '#') ret = shell_exec_line(ctx, line);
    }
    fclose(f);
    return ret;
}

/* ══════════════════════════════════════════════════════════════════════════
   Prompt expansion
   ══════════════════════════════════════════════════════════════════════════ */

char *shell_expand_prompt(ShellContext *ctx, const char *fmt) {
    if (!fmt) fmt = "%~ $ ";
    char buf[1024]; int bi = 0;
    for (const char *p = fmt; *p && bi < 1020; p++) {
        if (*p == '%' && p[1]) {
            p++;
            switch (*p) {
                case '~': { /* Current dir, abbreviated */
                    const char *home = shell_getenv(ctx, "HOME");
                    char *cwd = str_dup(ctx->cwd);
                    if (home && str_startswith(cwd, home)) {
                        int hl = (int)strlen(home);
                        buf[bi++] = '~';
                        const char *rest = cwd + hl;
                        while (*rest && bi < 1020) buf[bi++] = *rest++;
                    } else {
                        const char *c = cwd;
                        while (*c && bi < 1020) buf[bi++] = *c++;
                    }
                    HeapFree(GetProcessHeap(), 0, cwd);
                    break;
                }
                case 'n': { /* Username */
                    char user[128] = {0};
                    DWORD ul = sizeof(user);
                    GetUserNameA(user, &ul);
                    for (char *u = user; *u && bi < 1020; u++) buf[bi++] = *u;
                    break;
                }
                case 'm': { /* Hostname */
                    char host[128] = {0};
                    DWORD hl = sizeof(host);
                    GetComputerNameA(host, &hl);
                    for (char *h = host; *h && bi < 1020; h++) buf[bi++] = *h;
                    break;
                }
                case '$': buf[bi++] = (GetTokenInformation(GetCurrentProcessToken(), TokenElevation, NULL, 0, NULL) ? '#' : '$'); break;
                case '?': { char s[8]; _snprintf(s,sizeof(s),"%d",ctx->last_status); for(char*x=s;*x&&bi<1020;x++) buf[bi++]=*x; break; }
                case '%': buf[bi++] = '%'; break;
                default:  buf[bi++] = '%'; buf[bi++] = *p; break;
            }
        } else if (*p == '$') {
            /* Variable expansion in prompt */
            char *exp = expand_string(ctx, p);
            int el = (int)strlen(exp);
            if (bi + el < 1020) { memcpy(buf + bi, exp, el); bi += el; }
            HeapFree(GetProcessHeap(), 0, exp);
            while (*p) p++; /* skip to end */
            p--; /* outer loop will p++ */
        } else {
            buf[bi++] = *p;
        }
    }
    buf[bi] = '\0';
    return str_dup(buf);
}
