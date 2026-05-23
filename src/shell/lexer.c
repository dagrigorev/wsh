/*
 * lexer.c — ZSH-compatible tokeniser.
 *
 * Key design decisions:
 *   1. Quoting is resolved during tokenisation, not during expansion.
 *      Single-quoted text is stored verbatim (no escape processing).
 *      Double-quoted text is stored verbatim including $ and ` markers
 *      so the expander can process them on a second pass.
 *   2. Token text is arena-allocated — zero per-token heap traffic.
 *   3. Keyword recognition is done by string comparison in lex_next,
 *      not by a hash table; the keyword list is small enough that the
 *      linear scan is negligible.
 *
 * Edge cases handled:
 *   - Backslash-newline (line continuation) inside and outside quotes.
 *   - ANSI-C quoting $'...' is treated as a TOK_WORD with the literal text.
 *   - Heredoc marker tokenised as TOK_REDIR_HEREDOC + TOK_WORD.
 */
#include <windows.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "../core/str_util.h"

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static inline int  peek(const Lexer *l)    { return (unsigned char)l->src[l->pos]; }
static inline int  peek2(const Lexer *l)   { return l->src[l->pos] ? (unsigned char)l->src[l->pos+1] : 0; }
static inline void advance(Lexer *l)       { if (l->src[l->pos]) { if (l->src[l->pos]=='\n') l->line++; l->pos++; } }
static inline int  consume(Lexer *l)       { int c = peek(l); advance(l); return c; }

/* Dynamic byte buffer backed by the arena (no realloc; fixed max 8 KiB). */
#define TOKBUF_MAX 8192
typedef struct { char data[TOKBUF_MAX]; int len; } TokBuf;
static void tb_push(TokBuf *b, int c) { if (b->len < TOKBUF_MAX-1) b->data[b->len++] = (char)c; }
static char *tb_to_arena(TokBuf *b, Arena *a) { b->data[b->len] = '\0'; return arena_strdup(a, b->data); }

/* Skip whitespace (not newlines — newlines are meaningful). */
static void skip_ws(Lexer *l) {
    while (peek(l) == ' ' || peek(l) == '\t' || peek(l) == '\r') advance(l);
}

/* Skip to end of line (comment). */
static void skip_comment(Lexer *l) {
    while (peek(l) && peek(l) != '\n') advance(l);
}

/* Read a single-quoted span into buf; closing ' already consumed on return. */
static void read_single_quoted(Lexer *l, TokBuf *buf) {
    advance(l); /* consume opening ' */
    while (peek(l) && peek(l) != '\'') tb_push(buf, consume(l));
    if (peek(l) == '\'') advance(l); /* consume closing ' */
}

/* Read a double-quoted span; inner $, `, \ are preserved for the expander. */
static void read_double_quoted(Lexer *l, TokBuf *buf) {
    advance(l); /* consume opening " */
    while (peek(l) && peek(l) != '"') {
        if (peek(l) == '\\' && peek2(l)) {
            /* Preserve escape sequences the expander needs */
            tb_push(buf, '\\');
            advance(l);
            tb_push(buf, consume(l));
        } else {
            tb_push(buf, consume(l));
        }
    }
    if (peek(l) == '"') advance(l); /* consume closing " */
}

/* Read a $'...' ANSI-C quoted string; converts \e→ESC, \n→LF, \t→HT,
 * \ooo→octal, \xNN→hex.  Opening $' already consumed. */
static void read_ansi_c_quoted(Lexer *l, TokBuf *buf) {
    advance(l); /* consume $ */
    advance(l); /* consume opening \' */
    while (peek(l) && peek(l) != '\'') {
        if (peek(l) == '\\' && peek2(l)) {
            advance(l); /* consume \\ */
            int c = consume(l);
            switch (c) {
                case 'a':  tb_push(buf, '\a');  break;
                case 'b':  tb_push(buf, '\b');  break;
                case 'e':  tb_push(buf, '\x1B');break; /* ESC */
                case 'E':  tb_push(buf, '\x1B');break; /* ESC */
                case 'f':  tb_push(buf, '\f');  break;
                case 'n':  tb_push(buf, '\n');  break;
                case 'r':  tb_push(buf, '\r');  break;
                case 't':  tb_push(buf, '\t');  break;
                case 'v':  tb_push(buf, '\v');  break;
                case '\\': tb_push(buf, '\\'); break;
                case '\'':  tb_push(buf, '\'');  break;
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7': {
                    /* Octal: up to 3 digits */
                    int val = c - '0';
                    if (peek(l) >= '0' && peek(l) <= '7') val = val*8 + (consume(l)-'0');
                    if (peek(l) >= '0' && peek(l) <= '7') val = val*8 + (consume(l)-'0');
                    tb_push(buf, (char)val);
                    break;
                }
                case 'x': {
                    /* Hex: up to 2 digits */
                    int val = 0, nd = 0;
                    while (nd < 2 && ((peek(l) >= '0' && peek(l) <= '9') ||
                                      (peek(l) >= 'a' && peek(l) <= 'f') ||
                                      (peek(l) >= 'A' && peek(l) <= 'F'))) {
                        int d = consume(l);
                        val = val*16 + (d>='a'?d-'a'+10:d>='A'?d-'A'+10:d-'0');
                        nd++;
                    }
                    tb_push(buf, (char)val);
                    break;
                }
                default: tb_push(buf, '\\'); tb_push(buf, (char)c); break;
            }
        } else {
            tb_push(buf, consume(l));
        }
    }
    if (peek(l) == '\'') advance(l); /* consume closing \' */
}


/* Read one complete word token (handles quoting and concatenation). */
static Token read_word(Lexer *l, int start_line) {
    TokBuf buf = {0};
    bool   done = false;

    while (!done && peek(l)) {
        switch (peek(l)) {
            case '\'': read_single_quoted(l, &buf);  break;
            case '"':  read_double_quoted(l, &buf);  break;
            case '\\':
                advance(l);
                if (peek(l) == '\n') { advance(l); /* line continuation */ }
                else if (peek(l))    { tb_push(&buf, consume(l)); }
                break;
            /* Word-terminating characters */
            case ' ': case '\t': case '\r': case '\n':
            case '|': case '&':  case ';':
            case '<': case '>':
            case '(': case ')':
            case '{': case '}':
                done = true;
                break;
            case '$': {
                /* $'...' ANSI-C quoting: convert escape sequences to literal bytes */
                int nxt = peek2(l);
                if (nxt == '\'') {
                    read_ansi_c_quoted(l, &buf);
                } else if (nxt == '(' || nxt == '{') {
                    /* $(...) command substitution or ${...} parameter expansion:
                     * read verbatim (balanced) so word is not split at '(' or '{' */
                    tb_push(&buf, consume(l)); /* push '$' */
                    int open = consume(l);
                    char close = (open == '(') ? ')' : '}';
                    tb_push(&buf, (char)open);
                    int depth = 1;
                    while (peek(l) && depth > 0) {
                        int c2 = consume(l);
                        tb_push(&buf, (char)c2);
                        if (c2 == open)  depth++;
                        else if (c2 == close) depth--;
                    }
                } else {
                    tb_push(&buf, consume(l));
                }
                break;
            }
            default:
                tb_push(&buf, consume(l));
                break;
        }
    }

    /* Classify the word */
    char *text = tb_to_arena(&buf, l->arena);
    Token t    = { .kind = TOK_WORD, .text = text, .line = start_line, .fd = -1 };

    /* Assignment: valid-identifier '=' value */
    const char *eq = strchr(text, '=');
    if (eq && eq != text) {
        bool valid = true;
        for (const char *p = text; p != eq; p++) {
            if (!isalnum((unsigned char)*p) && *p != '_') { valid = false; break; }
        }
        if (valid) t.kind = TOK_ASSIGN;
    }

    return t;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void lex_init(Lexer *l, const char *src, Arena *arena) {
    l->src   = src ? src : "";
    l->pos   = 0;
    l->line  = 1;
    l->arena = arena;
}

Token lex_next(Lexer *l) {
    Token t = { .kind = TOK_EOF, .text = NULL, .line = l->line, .fd = -1 };

    for (;;) {  /* retry loop for whitespace / comments */
        skip_ws(l);
        t.line = l->line;

        int c = peek(l);
        if (!c) return t; /* TOK_EOF */

        if (c == '#') { skip_comment(l); continue; } /* re-loop */

        /* Top-level backslash-newline: line continuation between words */
        if (c == '\\' && l->src[l->pos + 1] == '\n') {
            advance(l); advance(l); /* skip both \ and \n */
            continue;
        }

        switch (c) {
            case '\n': advance(l); t.kind = TOK_NEWLINE; return t;

            case ';':
                advance(l);
                if (peek(l) == ';') { advance(l); t.kind = TOK_DSEMI; }
                else t.kind = TOK_SEMI;
                return t;

            case '&':
                advance(l);
                if (peek(l) == '&') { advance(l); t.kind = TOK_AND; }
                else t.kind = TOK_BG;
                return t;

            case '|':
                advance(l);
                if (peek(l) == '|') { advance(l); t.kind = TOK_OR; }
                else if (peek(l) == '&') { advance(l); t.kind = TOK_PIPE_ERR; }
                else t.kind = TOK_PIPE;
                return t;

            case '<':
                advance(l);
                if (peek(l) == '<') { advance(l); t.kind = TOK_REDIR_HEREDOC; }
                else t.kind = TOK_REDIR_IN;
                return t;

            case '>':
                advance(l);
                if (peek(l) == '&') {
                    advance(l);
                    TokBuf b = {0};
                    while (isdigit((unsigned char)peek(l))) tb_push(&b, consume(l));
                    t.kind = TOK_REDIR_DUP;
                    t.fd = 1;
                    t.text = tb_to_arena(&b, l->arena);
                } else if (peek(l) == '>') { advance(l); t.kind = TOK_REDIR_APPEND; }
                else t.kind = TOK_REDIR_OUT;
                return t;

            case '(': advance(l); t.kind = TOK_LPAREN; return t;
            case ')': advance(l); t.kind = TOK_RPAREN; return t;
            case '{': advance(l); t.kind = TOK_LBRACE; return t;
            case '}': advance(l); t.kind = TOK_RBRACE; return t;
            case '!': advance(l); t.kind = TOK_BANG;   return t;

            default:
                /* fd-prefixed redirection: digit(s) immediately followed by < or >
                 * e.g. 2>> 1> 2< — consume the fd, return the redirect token kind */
                if (isdigit((unsigned char)c)) {
                    int saved_pos = l->pos;
                    int fd = 0;
                    while (isdigit((unsigned char)peek(l))) fd = fd * 10 + (consume(l) - '0');
                    int nc = peek(l);
                    if (nc == '<' || nc == '>') {
                        advance(l); /* consume < or > */
                        t.fd = fd;
                        if (nc == '<') {
                            if (peek(l) == '<') { advance(l); t.kind = TOK_REDIR_HEREDOC; }
                            else t.kind = TOK_REDIR_IN;
                        } else {
                            if (peek(l) == '&') {
                                advance(l);
                                TokBuf b = {0};
                                while (isdigit((unsigned char)peek(l))) tb_push(&b, consume(l));
                                t.kind = TOK_REDIR_DUP;
                                t.text = tb_to_arena(&b, l->arena);
                            } else if (peek(l) == '>') { advance(l); t.kind = TOK_REDIR_APPEND; }
                            else t.kind = TOK_REDIR_OUT;
                        }
                        return t;
                    }
                    /* Not a fd-redirect — rewind and fall through to read_word */
                    l->pos = saved_pos;
                }
                return read_word(l, t.line);
        }
    }
}

const char *tok_name(TokenKind k) {
    switch (k) {
        case TOK_WORD:         return "WORD";
        case TOK_ASSIGN:       return "ASSIGN";
        case TOK_PIPE:         return "|";
        case TOK_PIPE_ERR:     return "|&";
        case TOK_AND:          return "&&";
        case TOK_OR:           return "||";
        case TOK_BG:           return "&";
        case TOK_SEMI:         return ";";
        case TOK_DSEMI:        return ";;";
        case TOK_NEWLINE:      return "NEWLINE";
        case TOK_REDIR_IN:     return "<";
        case TOK_REDIR_OUT:    return ">";
        case TOK_REDIR_APPEND: return ">>";
        case TOK_REDIR_HEREDOC:return "<<";
        case TOK_REDIR_DUP:    return ">&";
        case TOK_LPAREN:       return "(";
        case TOK_RPAREN:       return ")";
        case TOK_LBRACE:       return "{";
        case TOK_RBRACE:       return "}";
        case TOK_BANG:         return "!";
        case TOK_IF:           return "if";
        case TOK_THEN:         return "then";
        case TOK_ELSE:         return "else";
        case TOK_ELIF:         return "elif";
        case TOK_FI:           return "fi";
        case TOK_WHILE:        return "while";
        case TOK_UNTIL:        return "until";
        case TOK_DO:           return "do";
        case TOK_DONE:         return "done";
        case TOK_FOR:          return "for";
        case TOK_IN:           return "in";
        case TOK_CASE:         return "case";
        case TOK_ESAC:         return "esac";
        case TOK_FUNCTION:     return "function";
        case TOK_EOF:          return "EOF";
        default:               return "?";
    }
}
