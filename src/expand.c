#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "expand.h"
#include "shell.h"
#include "util.h"

/* ─── WordList ───────────────────────────────────────────────────────────── */

void wordlist_init(WordList *wl) { memset(wl, 0, sizeof(*wl)); }

void wordlist_push(WordList *wl, char *word) {
    if (wl->count >= wl->cap) {
        wl->cap  = wl->cap ? wl->cap * 2 : 8;
        wl->words = (char **)HeapReAlloc(GetProcessHeap(), 0, wl->words,
                                          (size_t)wl->cap * sizeof(char *));
    }
    wl->words[wl->count++] = word;
}

void wordlist_free(WordList *wl) {
    for (int i = 0; i < wl->count; i++) HeapFree(GetProcessHeap(), 0, wl->words[i]);
    HeapFree(GetProcessHeap(), 0, wl->words);
    memset(wl, 0, sizeof(*wl));
}

/* ─── Dynamic string builder ─────────────────────────────────────────────── */

typedef struct { char *buf; int len; int cap; } SB;

static void sb_init(SB *s) { memset(s, 0, sizeof(*s)); }
static void sb_push(SB *s, char c) {
    if (s->len + 1 >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->buf = (char *)HeapReAlloc(GetProcessHeap(), 0, s->buf, (size_t)s->cap);
    }
    s->buf[s->len++] = c;
    s->buf[s->len]   = '\0';
}
static void sb_append(SB *s, const char *t) {
    if (!t) return;
    while (*t) sb_push(s, *t++);
}
static char *sb_take(SB *s) {
    char *r = s->buf ? s->buf : str_dup("");
    s->buf = NULL; s->len = s->cap = 0;
    return r;
}
static void sb_free(SB *s) { if (s->buf) HeapFree(GetProcessHeap(), 0, s->buf); }

/* ─── Tilde expansion ────────────────────────────────────────────────────── */

char *expand_tilde(ShellContext *ctx, const char *s) {
    if (!s || s[0] != '~') return str_dup(s);
    const char *rest = s + 1;
    if (*rest == '/' || *rest == '\\' || *rest == '\0') {
        const char *home = shell_getenv(ctx, "HOME");
        if (!home) home = shell_getenv(ctx, "USERPROFILE");
        if (!home) home = "";
        if (*rest) rest++;
        return path_join(home, rest);
    }
    return str_dup(s);
}

/* ─── Variable expansion ─────────────────────────────────────────────────── */

static char *get_var(ShellContext *ctx, const char *name) {
    const char *v = shell_getenv(ctx, name);
    return v ? str_dup(v) : str_dup("");
}

/* Parse ${name[...][:-/...]} forms */
static char *expand_param(ShellContext *ctx, const char *spec) {
    char name[256] = {0};
    const char *p = spec;
    int i = 0;

    /* #VAR — string length */
    if (*p == '#' && p[1]) {
        p++;
        while (*p && *p != '}' && *p != ':' && *p != '/' && i < 255)
            name[i++] = *p++;
        char *v = get_var(ctx, name);
        char lenbuf[32];
        _snprintf(lenbuf, sizeof(lenbuf), "%d", (int)strlen(v));
        HeapFree(GetProcessHeap(), 0, v);
        return str_dup(lenbuf);
    }

    /* Collect var name */
    while (*p && *p != '}' && *p != ':' && *p != '/' && *p != '%' &&
           *p != '#' && *p != '?' && *p != '+' && i < 255)
        name[i++] = *p++;
    name[i] = '\0';

    char *val = get_var(ctx, name);
    bool  empty = (val[0] == '\0');

    if (*p == ':') {
        p++;
        char op = *p++;
        /* Everything after op is the argument */
        char arg[1024] = {0};
        int j = 0;
        while (*p && *p != '}' && j < 1023) arg[j++] = *p++;
        char *exp_arg = expand_string(ctx, arg);

        switch (op) {
            case '-': /* ${VAR:-default} */
                if (empty) { HeapFree(GetProcessHeap(),0,val); return exp_arg; }
                HeapFree(GetProcessHeap(),0,exp_arg); break;
            case '=': /* ${VAR:=default} */
                if (empty) {
                    shell_setenv(ctx, name, exp_arg, false);
                    HeapFree(GetProcessHeap(),0,val);
                    return exp_arg;
                }
                HeapFree(GetProcessHeap(),0,exp_arg); break;
            case '?': /* ${VAR:?msg} */
                if (empty) {
                    /* Treat as fatal error — print message */
                    wsh_log("${%s:?%s}", name, exp_arg);
                    HeapFree(GetProcessHeap(),0,exp_arg);
                    HeapFree(GetProcessHeap(),0,val);
                    return str_dup("");
                }
                HeapFree(GetProcessHeap(),0,exp_arg); break;
            case '+': /* ${VAR:+alt} */
                if (!empty) { HeapFree(GetProcessHeap(),0,val); return exp_arg; }
                HeapFree(GetProcessHeap(),0,exp_arg); break;
            default: HeapFree(GetProcessHeap(),0,exp_arg); break;
        }
    } else if (*p == '/') {
        /* ${VAR/pat/rep} */
        p++;
        bool global = (*p == '/');
        if (global) p++;
        const char *slash = strchr(p, '/');
        if (slash) {
            char pat[256] = {0}, rep[256] = {0};
            strncpy(pat, p, slash - p);
            strncpy(rep, slash + 1, 255);
            /* Simple substring replacement */
            char *found = strstr(val, pat);
            if (found) {
                SB sb; sb_init(&sb);
                sb_append(&sb, val);
                /* Replace first occurrence */
                char *v2 = sb_take(&sb);
                HeapFree(GetProcessHeap(),0,val);
                val = v2;
                (void)global; (void)rep; /* simplified */
            }
        }
    }
    return val;
}

/* ─── Arithmetic evaluator ───────────────────────────────────────────────── */

/* Recursive descent: +, -, *, /, % with precedence */
typedef struct { const char *p; ShellContext *ctx; } ArithCtx;

static long arith_expr(ArithCtx *a);

static long arith_primary(ArithCtx *a) {
    while (*a->p == ' ') a->p++;
    if (*a->p == '(') {
        a->p++;
        long v = arith_expr(a);
        if (*a->p == ')') a->p++;
        return v;
    }
    if (*a->p == '-') { a->p++; return -arith_primary(a); }
    if (*a->p == '+') { a->p++; return arith_primary(a); }
    if (*a->p == '~') { a->p++; return ~arith_primary(a); }
    if (*a->p == '!') { a->p++; return !arith_primary(a); }
    if (*a->p == '$') {
        a->p++;
        char name[64] = {0}; int i = 0;
        while (isalnum((unsigned char)*a->p) || *a->p == '_') name[i++] = *a->p++;
        const char *v = shell_getenv(a->ctx, name);
        return v ? atol(v) : 0;
    }
    /* Number */
    long v = 0;
    while (isdigit((unsigned char)*a->p)) v = v * 10 + (*a->p++ - '0');
    return v;
}

static long arith_mul(ArithCtx *a) {
    long l = arith_primary(a);
    while (*a->p == '*' || *a->p == '/' || *a->p == '%') {
        char op = *a->p++;
        long r = arith_primary(a);
        if (op == '*') l *= r;
        else if (op == '/' && r) l /= r;
        else if (op == '%' && r) l %= r;
    }
    return l;
}

static long arith_add(ArithCtx *a) {
    long l = arith_mul(a);
    while (*a->p == '+' || *a->p == '-') {
        char op = *a->p++;
        long r = arith_mul(a);
        l = op == '+' ? l + r : l - r;
    }
    return l;
}

static long arith_cmp(ArithCtx *a) {
    long l = arith_add(a);
    while (*a->p == '<' || *a->p == '>' || *a->p == '=' || *a->p == '!') {
        if (a->p[0] == '<' && a->p[1] == '=') { a->p += 2; l = l <= arith_add(a); }
        else if (a->p[0] == '>' && a->p[1] == '=') { a->p += 2; l = l >= arith_add(a); }
        else if (a->p[0] == '=' && a->p[1] == '=') { a->p += 2; l = l == arith_add(a); }
        else if (a->p[0] == '!' && a->p[1] == '=') { a->p += 2; l = l != arith_add(a); }
        else if (*a->p == '<') { a->p++; l = l < arith_add(a); }
        else if (*a->p == '>') { a->p++; l = l > arith_add(a); }
        else break;
    }
    return l;
}

static long arith_expr(ArithCtx *a) {
    while (*a->p == ' ') a->p++;
    long l = arith_cmp(a);
    while (*a->p == '&' || *a->p == '|' || *a->p == '^') {
        char op = *a->p++;
        if (op == '&' && *a->p == '&') { a->p++; long r = arith_cmp(a); l = l && r; }
        else if (op == '|' && *a->p == '|') { a->p++; long r = arith_cmp(a); l = l || r; }
        else { long r = arith_cmp(a); l = (op=='&')?(l&r):(op=='|'?(l|r):(l^r)); }
    }
    return l;
}

long expand_arith(ShellContext *ctx, const char *expr) {
    ArithCtx a = { .p = expr, .ctx = ctx };
    return arith_expr(&a);
}

char *expand_arith_str(ShellContext *ctx, const char *expr) {
    long v = expand_arith(ctx, expr);
    char buf[64];
    _snprintf(buf, sizeof(buf), "%ld", v);
    return str_dup(buf);
}

/* ─── Command substitution ───────────────────────────────────────────────── */

char *expand_command_subst(ShellContext *ctx, const char *cmd) {
    /* Create a pipe, run shell_exec_line capturing output */
    HANDLE hread = INVALID_HANDLE_VALUE, hwrite = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hread, &hwrite, &sa, 0)) return str_dup("");

    /* Swap stdout */
    HANDLE old_stdout = ctx->h_stdout;
    ctx->h_stdout = hwrite;

    shell_exec_line(ctx, cmd);

    ctx->h_stdout = old_stdout;
    CloseHandle(hwrite);

    /* Read output */
    SB sb; sb_init(&sb);
    char buf[4096]; DWORD n;
    while (ReadFile(hread, buf, sizeof(buf) - 1, &n, NULL) && n > 0) {
        buf[n] = '\0';
        sb_append(&sb, buf);
    }
    CloseHandle(hread);

    char *out = sb_take(&sb);
    /* Strip trailing newlines */
    int l = (int)strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
    return out;
}

/* ─── Main expand_string ─────────────────────────────────────────────────── */

char *expand_string(ShellContext *ctx, const char *s) {
    if (!s) return str_dup("");
    SB sb; sb_init(&sb);
    const char *p = s;
    while (*p) {
        if (*p == '~' && p == s && (p[1] == '/' || p[1] == '\\' || !p[1])) {
            char *t = expand_tilde(ctx, p);
            sb_append(&sb, t);
            HeapFree(GetProcessHeap(), 0, t);
            p += strlen(p);
        } else if (*p == '$') {
            p++;
            if (*p == '{') {
                /* ${...} */
                p++;
                const char *start = p;
                int depth = 1;
                while (*p && depth > 0) { if (*p=='{') depth++; else if (*p=='}') depth--; p++; }
                char spec[512] = {0};
                strncpy(spec, start, (size_t)((p - 1) - start));
                char *v = expand_param(ctx, spec);
                sb_append(&sb, v);
                HeapFree(GetProcessHeap(), 0, v);
            } else if (*p == '(') {
                p++;
                if (*p == '(') {
                    /* $(( arith )) */
                    p++;
                    const char *start = p;
                    int depth = 1;
                    while (*p && depth > 0) {
                        if (*p=='(' && p[1]=='(') { depth++; p+=2; }
                        else if (*p==')' && p[1]==')') { if (!--depth) break; else p+=2; }
                        else p++;
                    }
                    char expr[512] = {0};
                    strncpy(expr, start, (size_t)(p - start));
                    char *v = expand_arith_str(ctx, expr);
                    sb_append(&sb, v);
                    HeapFree(GetProcessHeap(), 0, v);
                    if (*p == ')') p++;
                    if (*p == ')') p++;
                } else {
                    /* $( cmd ) */
                    const char *start = p;
                    int depth = 1;
                    while (*p && depth > 0) { if (*p=='(') depth++; else if (*p==')') depth--; p++; }
                    char cmd[1024] = {0};
                    strncpy(cmd, start, (size_t)((p - 1) - start));
                    char *v = expand_command_subst(ctx, cmd);
                    sb_append(&sb, v);
                    HeapFree(GetProcessHeap(), 0, v);
                }
            } else if (*p == '?') {
                char buf[16]; _snprintf(buf, sizeof(buf), "%d", ctx->last_status);
                sb_append(&sb, buf); p++;
            } else if (*p == '$') {
                char buf[16]; _snprintf(buf, sizeof(buf), "%lu", ctx->shell_pid);
                sb_append(&sb, buf); p++;
            } else if (*p == '!') {
                char buf[16]; _snprintf(buf, sizeof(buf), "%lu", ctx->last_bg_pid);
                sb_append(&sb, buf); p++;
            } else if (isalpha((unsigned char)*p) || *p == '_') {
                /* $NAME */
                char name[256] = {0}; int i = 0;
                while (isalnum((unsigned char)*p) || *p == '_') name[i++] = *p++;
                char *v = get_var(ctx, name);
                sb_append(&sb, v);
                HeapFree(GetProcessHeap(), 0, v);
            } else {
                sb_push(&sb, '$');
            }
        } else if (*p == '`') {
            /* backtick command substitution */
            p++;
            const char *start = p;
            while (*p && *p != '`') p++;
            char cmd[1024] = {0};
            strncpy(cmd, start, (size_t)(p - start));
            if (*p == '`') p++;
            char *v = expand_command_subst(ctx, cmd);
            sb_append(&sb, v);
            HeapFree(GetProcessHeap(), 0, v);
        } else {
            sb_push(&sb, *p++);
        }
    }
    return sb_take(&sb);
}

/* ─── Brace expansion ────────────────────────────────────────────────────── */

WordList expand_brace(const char *s) {
    WordList wl; wordlist_init(&wl);
    /* Find outermost { } */
    const char *lbrace = strchr(s, '{');
    if (!lbrace) { wordlist_push(&wl, str_dup(s)); return wl; }
    const char *rbrace = strrchr(s, '}');
    if (!rbrace || rbrace <= lbrace) { wordlist_push(&wl, str_dup(s)); return wl; }

    char prefix[512] = {0};
    strncpy(prefix, s, (size_t)(lbrace - s));
    char suffix[512] = {0};
    strncpy(suffix, rbrace + 1, 511);

    char inner[512] = {0};
    strncpy(inner, lbrace + 1, (size_t)(rbrace - lbrace - 1));

    /* Check for numeric range: {1..10} */
    int range_start = 0, range_end = 0;
    if (sscanf(inner, "%d..%d", &range_start, &range_end) == 2) {
        int step = range_start <= range_end ? 1 : -1;
        for (int n = range_start; n != range_end + step; n += step) {
            char word[256]; _snprintf(word, sizeof(word), "%s%d%s", prefix, n, suffix);
            wordlist_push(&wl, str_dup(word));
        }
        return wl;
    }

    /* Comma-separated alternatives */
    char *tok = str_dup(inner);
    char *p = tok, *start = tok;
    int depth = 0;
    while (*p) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            char word[1024]; _snprintf(word, sizeof(word), "%s%s%s", prefix, start, suffix);
            wordlist_push(&wl, str_dup(word));
            start = p + 1;
        }
        p++;
    }
    char word[1024]; _snprintf(word, sizeof(word), "%s%s%s", prefix, start, suffix);
    wordlist_push(&wl, str_dup(word));
    HeapFree(GetProcessHeap(), 0, tok);
    return wl;
}

/* ─── Glob expansion ─────────────────────────────────────────────────────── */

static void glob_collect(const char *dir, const char *pattern, WordList *wl, bool recursive) {
    char search[MAX_PATH];
    _snprintf(search, MAX_PATH, "%s\\*", dir[0] ? dir : ".");

    wchar_t *wsearch = utf8_to_utf16(search, NULL);
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(wsearch, &fd);
    HeapFree(GetProcessHeap(), 0, wsearch);
    if (hf == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        char *name = utf16_to_utf8(fd.cFileName, NULL);
        if (!name) continue;

        /* Match against pattern */
        wchar_t *wpat = utf8_to_utf16(pattern, NULL);
        bool match = wpat ? (PathMatchSpecW(fd.cFileName, wpat) == TRUE) : false;
        if (wpat) HeapFree(GetProcessHeap(), 0, wpat);

        if (match) {
            char full[MAX_PATH];
            if (dir[0]) _snprintf(full, MAX_PATH, "%s\\%s", dir, name);
            else strncpy(full, name, MAX_PATH-1);
            wordlist_push(wl, str_dup(full));
        }

        if (recursive && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char subdir[MAX_PATH];
            if (dir[0]) _snprintf(subdir, MAX_PATH, "%s\\%s", dir, name);
            else strncpy(subdir, name, MAX_PATH-1);
            glob_collect(subdir, pattern, wl, true);
        }
        HeapFree(GetProcessHeap(), 0, name);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

WordList expand_glob(const char *pattern) {
    WordList wl; wordlist_init(&wl);
    if (!pattern) return wl;

    /* Check for recursive ** */
    bool recursive = strstr(pattern, "**") != NULL;

    /* Split pattern into dir + file parts */
    char dir[MAX_PATH] = {0}, file_pat[MAX_PATH] = {0};
    const char *last_sep = strrchr(pattern, '\\');
    const char *last_fwd = strrchr(pattern, '/');
    const char *sep = last_sep > last_fwd ? last_sep : last_fwd;
    if (sep) {
        strncpy(dir, pattern, (size_t)(sep - pattern));
        strncpy(file_pat, sep + 1, MAX_PATH-1);
    } else {
        file_pat[0] = '\0';
        strncpy(file_pat, pattern, MAX_PATH-1);
    }

    /* If no wildcard characters, return as-is */
    if (!strchr(file_pat, '*') && !strchr(file_pat, '?') && !strchr(file_pat, '[')) {
        wordlist_push(&wl, str_dup(pattern));
        return wl;
    }

    glob_collect(dir, file_pat, &wl, recursive);

    /* If no matches, return pattern unchanged */
    if (wl.count == 0) wordlist_push(&wl, str_dup(pattern));
    return wl;
}

/* ─── IFS split ──────────────────────────────────────────────────────────── */

WordList split_ifs(ShellContext *ctx, const char *s) {
    WordList wl; wordlist_init(&wl);
    const char *ifs = shell_getenv(ctx, "IFS");
    if (!ifs) ifs = " \t\n";

    SB word; sb_init(&word);
    for (const char *p = s; *p; p++) {
        if (strchr(ifs, *p)) {
            if (word.len > 0) { wordlist_push(&wl, sb_take(&word)); }
        } else {
            sb_push(&word, *p);
        }
    }
    if (word.len > 0) wordlist_push(&wl, sb_take(&word));
    else sb_free(&word);
    return wl;
}

/* ─── expand_word: main entry point ─────────────────────────────────────── */

WordList expand_word(ShellContext *ctx, const char *word, bool glob_ok, bool split_ok) {
    WordList result; wordlist_init(&result);
    if (!word) return result;

    /* 1. Handle single-quoted strings — literal, no expansion */
    if (word[0] == '\'' && word[strlen(word)-1] == '\'') {
        char *inner = str_ndup(word + 1, strlen(word) - 2);
        wordlist_push(&result, inner);
        return result;
    }

    /* 2. Strip double quotes for expansion but allow $, ` */
    bool double_quoted = (word[0] == '"' && word[strlen(word)-1] == '"');
    const char *src = double_quoted ? word : word;

    /* 3. Expand string (handles $, ` inside double-quotes too) */
    char *expanded = expand_string(ctx, src);

    /* If double-quoted: no glob, no word split */
    if (double_quoted) {
        /* Remove surrounding quotes if still present */
        int l = (int)strlen(expanded);
        if (l >= 2 && expanded[0] == '"' && expanded[l-1] == '"') {
            char *inner = str_ndup(expanded + 1, (size_t)(l - 2));
            HeapFree(GetProcessHeap(), 0, expanded);
            expanded = inner;
        }
        wordlist_push(&result, expanded);
        return result;
    }

    /* 4. Brace expansion */
    WordList braced = expand_brace(expanded);
    HeapFree(GetProcessHeap(), 0, expanded);

    for (int i = 0; i < braced.count; i++) {
        char *w = braced.words[i];
        braced.words[i] = NULL; /* take ownership */

        if (split_ok) {
            WordList parts = split_ifs(ctx, w);
            HeapFree(GetProcessHeap(), 0, w);
            for (int j = 0; j < parts.count; j++) {
                char *pw = parts.words[j]; parts.words[j] = NULL;
                if (glob_ok) {
                    WordList globs = expand_glob(pw);
                    HeapFree(GetProcessHeap(), 0, pw);
                    for (int k = 0; k < globs.count; k++) {
                        wordlist_push(&result, globs.words[k]);
                        globs.words[k] = NULL;
                    }
                    wordlist_free(&globs);
                } else {
                    wordlist_push(&result, pw);
                }
            }
            wordlist_free(&parts);
        } else if (glob_ok) {
            WordList globs = expand_glob(w);
            HeapFree(GetProcessHeap(), 0, w);
            for (int k = 0; k < globs.count; k++) {
                wordlist_push(&result, globs.words[k]);
                globs.words[k] = NULL;
            }
            wordlist_free(&globs);
        } else {
            wordlist_push(&result, w);
        }
    }
    wordlist_free(&braced);
    return result;
}

char *expand_variable(ShellContext *ctx, const char *s) {
    return expand_string(ctx, s);
}
