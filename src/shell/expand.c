/*
 * expand.c — Word expansion for the Wsh shell.
 *
 * Design decisions:
 *   - A string-builder type (SB) accumulates expanded output avoiding O(n²)
 *     string concatenation.
 *   - Arithmetic evaluation is a proper recursive-descent expression parser
 *     (not strtol + eval tricks) so operator precedence is correct.
 *   - Glob matching uses Win32 FindFirstFileW with PathMatchSpecW for
 *     pattern matching, falling back to the literal pattern when no matches
 *     exist (POSIX "nullglob-off" default).
 *   - Command substitution swaps ctx->io for a buffer-capturing adapter,
 *     runs the command, then restores io. Clean, no temp files.
 */
#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "expand.h"
#include "shell_ctx.h"
#include "env.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"

#pragma comment(lib, "shlwapi.lib")

/* ── String builder ───────────────────────────────────────────────────────── */

typedef struct { char *buf; int len; int cap; } SB;

static void sb_push(SB *b, char c) {
    if (b->len >= b->cap - 1) {
        b->cap = b->cap ? b->cap * 2 : 256;
        if (b->buf) {
            b->buf = (char *)HeapReAlloc(GetProcessHeap(), 0, b->buf, (size_t)b->cap);
        } else {
            b->buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)b->cap);
        }
    }
    b->buf[b->len++] = c;
}
static void sb_push_str(SB *b, const char *s) { for (; *s; s++) sb_push(b, *s); }
static char *sb_take(SB *b) {
    sb_push(b, '\0');
    char *r = b->buf; b->buf = NULL; b->len = b->cap = 0; return r;
}
static void sb_free(SB *b) { if (b->buf) HeapFree(GetProcessHeap(), 0, b->buf); }

/* ── WordList ─────────────────────────────────────────────────────────────── */

void wordlist_init(WordList *wl) { memset(wl, 0, sizeof(*wl)); }
void wordlist_push(WordList *wl, char *word) {
    if (wl->count >= wl->cap) {
        wl->cap = wl->cap ? wl->cap * 2 : 8;
        size_t bytes = (size_t)wl->cap * sizeof(char *);
        if (wl->words) {
            wl->words = (char **)HeapReAlloc(GetProcessHeap(), 0, wl->words, bytes);
        } else {
            wl->words = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
        }
    }
    wl->words[wl->count++] = word;
}
void wordlist_free(WordList *wl) {
    for (int i = 0; i < wl->count; i++) str_free(wl->words[i]);
    if (wl->words) HeapFree(GetProcessHeap(), 0, wl->words);
    memset(wl, 0, sizeof(*wl));
}

/* ── Tilde expansion ──────────────────────────────────────────────────────── */

char *expand_tilde(ShellContext *ctx, const char *s) {
    if (!s || s[0] != '~') return str_dup(s ? s : "");
    const char *home = shell_getenv(ctx, "HOME");
    if (!home) home = shell_getenv(ctx, "USERPROFILE");
    if (!home) return str_dup(s);
    const char *rest = s + 1;
    if (*rest == '\\' || *rest == '/' || *rest == '\0') {
        if (*rest) rest++;
        return path_join(home, rest);
    }
    return str_dup(s);
}

/* ── Variable expansion ───────────────────────────────────────────────────── */

static char *expand_param(ShellContext *ctx, const char *spec) {
    if (spec[0] == '#') {
        const char *val = shell_getenv(ctx, spec + 1);
        char buf[32]; _snprintf(buf, sizeof(buf), "%d", val ? (int)strlen(val) : 0);
        return str_dup(buf);
    }
    /* Scan past the variable name to find the operator position. */
    const char *op = spec;
    while (isalnum((unsigned char)*op) || *op == '_') op++;
    if (!*op) return str_dup(shell_getenv(ctx, spec) ? shell_getenv(ctx, spec) : "");
    char name[256] = {0};
    size_t nlen = (size_t)(op - spec); if (nlen > 255) nlen = 255;
    strncpy(name, spec, nlen);
    const char *val = shell_getenv(ctx, name);
    if (op[0] == ':' && op[1] == '-') return str_dup((val && val[0]) ? val : op + 2);
    if (op[0] == ':' && op[1] == '=') {
        if (!val || !val[0]) { shell_setenv(ctx, name, op + 2, false); val = op + 2; }
        return str_dup(val);
    }
    if (op[0] == ':' && op[1] == '?') {
        if (!val || !val[0]) {
            char msg[512]; _snprintf(msg, sizeof(msg), "%s: %s", name, op + 2);
            io_writeln(ctx->io, msg); return str_dup("");
        }
        return str_dup(val);
    }
    if (op[0] == ':' && op[1] == '+') return str_dup((val && val[0]) ? op + 2 : "");
    if (op[0] == '/') {
        if (!val) return str_dup("");
        bool global = (op[1] == '/');
        const char *pat_start = op + (global ? 2 : 1);
        const char *slash2 = strchr(pat_start, '/');
        if (!slash2) return str_dup(val);
        char pat[256] = {0}, rep[256] = {0};
        strncpy(pat, pat_start, (size_t)(slash2 - pat_start));
        strncpy(rep, slash2 + 1, 255);
        SB sb = {0}; const char *p = val; size_t patlen = strlen(pat); bool replaced = false;
        while (*p) {
            if (strncmp(p, pat, patlen) == 0 && (global || !replaced))
                { sb_push_str(&sb, rep); p += patlen; replaced = true; }
            else sb_push(&sb, *p++);
        }
        return sb_take(&sb);
    }
    return str_dup(val ? val : "");
}

/* ── Command substitution ─────────────────────────────────────────────────── */
/*
 * Capture IO adapter that writes into a local SB.  No globals — this is
 * reentrant and safe for nested $() substitutions.
 */

typedef struct { IShellIO base; SB *buf; } CapIO;

static void cap_write(IShellIO *self, const char *buf, int len) {
    SB *sb = ((CapIO *)self)->buf;
    for (int i = 0; i < len; i++) sb_push(sb, buf[i]);
}
static int cap_read_noop(IShellIO *self, char *buf, int size) {
    (void)self; (void)buf; (void)size; return 0;
}

static char *expand_cmd_subst(ShellContext *ctx, const char *cmd) {
    SB cap = {0};
    CapIO cio = { { cap_write, cap_read_noop }, &cap };
    IShellIO *saved = ctx->io;
    ctx->io = &cio.base;
    shell_exec_line(ctx, cmd);
    ctx->io = saved;
    char *result = sb_take(&cap);
    if (result) {
        char *end = result + strlen(result);
        while (end > result && (end[-1] == '\n' || end[-1] == '\r')) end--;
        *end = '\0';
    }
    return result ? result : str_dup("");
}

/* ── Arithmetic evaluator ─────────────────────────────────────────────────── */

typedef struct { const char *p; ShellContext *ctx; } ArithCtx;
static long arith_expr(ArithCtx *a);
static void arith_skip_ws(ArithCtx *a) { while (*a->p == ' ' || *a->p == '\t') a->p++; }

static long arith_primary(ArithCtx *a) {
    arith_skip_ws(a);
    if (*a->p == '(') { a->p++; long v = arith_expr(a); arith_skip_ws(a); if (*a->p == ')') a->p++; return v; }
    if (*a->p == '-') { a->p++; return -arith_primary(a); }
    if (*a->p == '+') { a->p++; return  arith_primary(a); }
    if (*a->p == '!') { a->p++; return !arith_primary(a); }
    if (*a->p == '~') { a->p++; return ~arith_primary(a); }
    if (*a->p == '$') {
        a->p++;
        if (*a->p == '{') {
            a->p++; const char *s = a->p;
            while (*a->p && *a->p != '}') a->p++;
            char name[256] = {0}; strncpy(name, s, (size_t)(a->p - s));
            if (*a->p == '}') a->p++;
            const char *val = shell_getenv(a->ctx, name); return val ? atol(val) : 0;
        }
        const char *s = a->p;
        while (isalnum((unsigned char)*a->p) || *a->p == '_') a->p++;
        char name[256] = {0}; strncpy(name, s, (size_t)(a->p - s));
        const char *val = shell_getenv(a->ctx, name); return val ? atol(val) : 0;
    }
    if (isalpha((unsigned char)*a->p) || *a->p == '_') {
        const char *s = a->p;
        while (isalnum((unsigned char)*a->p) || *a->p == '_') a->p++;
        char name[256] = {0}; strncpy(name, s, (size_t)(a->p - s));
        const char *val = shell_getenv(a->ctx, name); return val ? atol(val) : 0;
    }
    if (isdigit((unsigned char)*a->p)) { char *end; long v = strtol(a->p, &end, 0); a->p = end; return v; }
    return 0;
}

static long arith_mul(ArithCtx *a) {
    long left = arith_primary(a); arith_skip_ws(a);
    while (*a->p == '*' || *a->p == '/' || *a->p == '%') {
        char op = *a->p++; long right = arith_primary(a); arith_skip_ws(a);
        if (op=='*') left*=right; else if (op=='/') left=right?left/right:0; else left=right?left%right:0;
    }
    return left;
}
static long arith_add(ArithCtx *a) {
    long left = arith_mul(a); arith_skip_ws(a);
    while (*a->p=='+' || *a->p=='-') { char op=*a->p++; long r=arith_mul(a); arith_skip_ws(a); left=op=='+'?left+r:left-r; }
    return left;
}
static long arith_shift(ArithCtx *a) {
    long left = arith_add(a); arith_skip_ws(a);
    while ((a->p[0]=='<'&&a->p[1]=='<')||(a->p[0]=='>'&&a->p[1]=='>')) {
        bool ls=a->p[0]=='<'; a->p+=2; long r=arith_add(a); arith_skip_ws(a);
        left=ls?(left<<r):(left>>r);
    }
    return left;
}
static long arith_cmp(ArithCtx *a) {
    long left = arith_shift(a); arith_skip_ws(a);
    while (*a->p=='<'||*a->p=='>') {
        bool lt=*a->p=='<'; bool eq=a->p[1]=='='; a->p+=eq?2:1; long r=arith_shift(a); arith_skip_ws(a);
        if (lt) left=eq?(left<=r):(left<r); else left=eq?(left>=r):(left>r);
    }
    return left;
}
static long arith_eq(ArithCtx *a) {
    long left = arith_cmp(a); arith_skip_ws(a);
    while ((a->p[0]=='='&&a->p[1]=='=')||(a->p[0]=='!'&&a->p[1]=='=')) {
        bool eq=a->p[0]=='='; a->p+=2; long r=arith_cmp(a); arith_skip_ws(a);
        left=eq?(left==r):(left!=r);
    }
    return left;
}
static long arith_bitand(ArithCtx *a) {
    long left=arith_eq(a); arith_skip_ws(a);
    while (a->p[0]=='&'&&a->p[1]!='&') { a->p++; left&=arith_eq(a); arith_skip_ws(a); }
    return left;
}
static long arith_bitor(ArithCtx *a) {
    long left=arith_bitand(a); arith_skip_ws(a);
    while (a->p[0]=='|'&&a->p[1]!='|') { a->p++; left|=arith_bitand(a); arith_skip_ws(a); }
    return left;
}
static long arith_logand(ArithCtx *a) {
    long left=arith_bitor(a); arith_skip_ws(a);
    while (a->p[0]=='&'&&a->p[1]=='&') { a->p+=2; long r=arith_bitor(a); arith_skip_ws(a); left=left&&r; }
    return left;
}
static long arith_logor(ArithCtx *a) {
    long left=arith_logand(a); arith_skip_ws(a);
    while (a->p[0]=='|'&&a->p[1]=='|') { a->p+=2; long r=arith_logand(a); arith_skip_ws(a); left=left||r; }
    return left;
}
static long arith_ternary(ArithCtx *a) {
    long cond=arith_logor(a); arith_skip_ws(a);
    if (*a->p=='?') {
        a->p++; long t=arith_ternary(a); arith_skip_ws(a);
        if (*a->p==':') a->p++; long f=arith_ternary(a); return cond?t:f;
    }
    return cond;
}
static long arith_expr(ArithCtx *a) { return arith_ternary(a); }

long expand_arith(ShellContext *ctx, const char *expr) {
    if (!expr) return 0;
    ArithCtx a; a.p = expr; a.ctx = ctx;
    return arith_expr(&a);
}

/* ── String expansion ─────────────────────────────────────────────────────── */

char *expand_string(ShellContext *ctx, const char *s) {
    if (!s) return str_dup("");
    SB out = {0};
    for (const char *p = s; *p; ) {
        if (*p != '$' && *p != '`' && *p != '\\') { sb_push(&out, *p++); continue; }
        if (*p == '\\') { p++; if (*p) sb_push(&out, *p++); continue; }
        if (*p == '`') {
            p++; const char *start = p;
            while (*p && *p != '`') p++;
            char *cmd = str_ndup(start, (size_t)(p - start));
            char *res = expand_cmd_subst(ctx, cmd); str_free(cmd);
            if (res) { sb_push_str(&out, res); str_free(res); }
            if (*p == '`') p++;
            continue;
        }
        p++; /* skip $ */
        /* $'...' ANSI-C quoting inline in expand_string */
        if (*p == '\'') {
            p++; /* skip opening ' */
            while (*p && *p != '\'') {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                        case 'e': case 'E': sb_push(&out, '\x1B'); break;
                        case 'n': sb_push(&out, '\n'); break;
                        case 'r': sb_push(&out, '\r'); break;
                        case 't': sb_push(&out, '\t'); break;
                        case '\\': sb_push(&out, '\\'); break;
                        case '\'': sb_push(&out, '\''); break;
                        case '0': case '1': case '2': case '3':
                        case '4': case '5': case '6': case '7': {
                            int val = *p - '0';
                            if (p[1]>='0'&&p[1]<='7') { p++; val=val*8+(*p-'0'); }
                            if (p[1]>='0'&&p[1]<='7') { p++; val=val*8+(*p-'0'); }
                            sb_push(&out, (char)val); break;
                        }
                        default: sb_push(&out, '\\'); sb_push(&out, *p); break;
                    }
                    p++;
                } else {
                    sb_push(&out, *p++);
                }
            }
            if (*p == '\'') p++; /* skip closing ' */
            continue;
        }
        if (*p == '(') {
            p++;
            if (*p == '(') {
                p++; const char *start = p; int depth = 2;
                while (*p && depth > 0) { if (*p=='(')depth++; if (*p==')')depth--; if(depth>0)p++; else p++; }
                char *expr = str_ndup(start, (size_t)(p - start - 2));
                char *xexpr = expand_string(ctx, expr); str_free(expr);
                long val = expand_arith(ctx, xexpr); str_free(xexpr);
                char buf[32]; _snprintf(buf, sizeof(buf), "%ld", val); sb_push_str(&out, buf);
            } else {
                int depth = 1; const char *start = p;
                while (*p && depth > 0) { if(*p=='(')depth++; else if(*p==')')depth--; if(depth>0)p++; else p++; }
                char *cmd = str_ndup(start, (size_t)(p - start - 1));
                char *res = expand_cmd_subst(ctx, cmd); str_free(cmd);
                if (res) { sb_push_str(&out, res); str_free(res); }
            }
            continue;
        }
        if (*p == '{') {
            p++; const char *start = p; int depth = 1;
            while (*p && depth > 0) { if(*p=='{')depth++; else if(*p=='}')depth--; if(depth>0)p++; else p++; }
            char *spec = str_ndup(start, (size_t)(p - start - 1));
            char *val  = expand_param(ctx, spec); str_free(spec);
            if (val) { sb_push_str(&out, val); str_free(val); }
            continue;
        }
        if (*p=='?') { char buf[16]; _snprintf(buf,sizeof(buf),"%d",ctx->last_status); sb_push_str(&out,buf); p++; continue; }
        if (*p=='$') { char buf[16]; _snprintf(buf,sizeof(buf),"%lu",(unsigned long)ctx->shell_pid); sb_push_str(&out,buf); p++; continue; }
        if (*p=='!') { char buf[16]; _snprintf(buf,sizeof(buf),"%lu",(unsigned long)ctx->last_bg_pid); sb_push_str(&out,buf); p++; continue; }
        if (*p=='#') { char buf[16]; _snprintf(buf,sizeof(buf),"%d",ctx->positional_count); sb_push_str(&out,buf); p++; continue; }
        if (*p=='@'||*p=='*') {
            for (int i=0; i<ctx->positional_count; i++) { if(i>0)sb_push(&out,' '); sb_push_str(&out,ctx->positionals[i]); }
            p++; continue;
        }
        if (isdigit((unsigned char)*p)) {
            int idx=*p++-'0';
            if (idx>0&&idx<=ctx->positional_count) sb_push_str(&out,ctx->positionals[idx-1]);
            continue;
        }
        if (isalpha((unsigned char)*p)||*p=='_') {
            const char *start=p;
            while (isalnum((unsigned char)*p)||*p=='_') p++;
            char name[256]={0}; strncpy(name,start,(size_t)(p-start));
            const char *val=shell_getenv(ctx,name); if(val) sb_push_str(&out,val);
            continue;
        }
        sb_push(&out, '$');
    }
    return sb_take(&out);
}

/* ── Brace expansion ──────────────────────────────────────────────────────── */

WordList expand_brace(const char *word) {
    WordList result; wordlist_init(&result);
    const char *ob = strchr(word, '{');
    if (!ob) { wordlist_push(&result, str_dup(word)); return result; }
    const char *cb = NULL; int depth = 0;
    for (const char *p = ob; *p; p++) {
        if (*p=='{') depth++; else if (*p=='}') { if(--depth==0){cb=p;break;} }
    }
    if (!cb) { wordlist_push(&result, str_dup(word)); return result; }
    char prefix[512]={0}, suffix[512]={0}, inner[1024]={0};
    strncpy(prefix, word, (size_t)(ob - word));
    strncpy(inner,  ob + 1, (size_t)(cb - ob - 1));
    strncpy(suffix, cb + 1, 511);
    int from, to;
    if (sscanf(inner, "%d..%d", &from, &to) == 2) {
        int step = from <= to ? 1 : -1;
        for (int i = from; i != to + step; i += step) {
            char item[32]; _snprintf(item, sizeof(item), "%s%d%s", prefix, i, suffix);
            WordList sub = expand_brace(item);
            for (int j=0; j<sub.count; j++) wordlist_push(&result, sub.words[j]);
            sub.count=0; wordlist_free(&sub);
        }
        return result;
    }
    char *items[64]; int item_count = 0;
    int d = 0; const char *seg_start = inner;
    for (const char *p = inner; ; p++) {
        if (*p=='{') d++; else if (*p=='}') d--;
        else if ((*p==','||*p=='\0') && d==0) {
            if (item_count<63) items[item_count++]=str_ndup(seg_start,(size_t)(p-seg_start));
            if (*p=='\0') break; seg_start=p+1;
        }
    }
    if (item_count < 2) {
        for (int i=0; i<item_count; i++) str_free(items[i]);
        wordlist_push(&result, str_dup(word)); return result;
    }
    for (int i = 0; i < item_count; i++) {
        char combined[1024]; _snprintf(combined, sizeof(combined), "%s%s%s", prefix, items[i], suffix);
        str_free(items[i]);
        WordList sub = expand_brace(combined);
        for (int j=0; j<sub.count; j++) wordlist_push(&result, sub.words[j]);
        sub.count=0; wordlist_free(&sub);
    }
    return result;
}

/* ── Glob expansion ───────────────────────────────────────────────────────── */

WordList expand_glob(const char *pattern) {
    WordList result; wordlist_init(&result);
    if (!pattern) return result;
    char dir[MAX_PATH]="."; char file_pat[MAX_PATH]={0};
    const char *last_sep=strrchr(pattern,'\\'); const char *last_fwd=strrchr(pattern,'/');
    const char *sep=last_sep>last_fwd?last_sep:last_fwd;
    if (sep) { strncpy(dir,pattern,(size_t)(sep-pattern)); strncpy(file_pat,sep+1,MAX_PATH-1); }
    else       strncpy(file_pat,pattern,MAX_PATH-1);
    if (!strpbrk(file_pat,"*?[")) { wordlist_push(&result,str_dup(pattern)); return result; }
    wchar_t wsearch[MAX_PATH]; swprintf(wsearch,MAX_PATH,L"%hs\\*",dir);
    wchar_t wpat[MAX_PATH]; MultiByteToWideChar(CP_UTF8,0,file_pat,-1,wpat,MAX_PATH);
    WIN32_FIND_DATAW fd; HANDLE hf=FindFirstFileW(wsearch,&fd);
    if (hf==INVALID_HANDLE_VALUE) { wordlist_push(&result,str_dup(pattern)); return result; }
    do {
        if (!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
        if (!PathMatchSpecW(fd.cFileName,wpat)) continue;
        char *name=u16_to_u8(fd.cFileName,NULL); if(!name) continue;
        char full[MAX_PATH]; _snprintf(full,MAX_PATH,"%s\\%s",strcmp(dir,".")==0?"":dir,name);
        wordlist_push(&result,str_dup(full[0]=='\\'?full+1:full)); str_free(name);
    } while (FindNextFileW(hf,&fd));
    FindClose(hf);
    if (result.count==0) wordlist_push(&result,str_dup(pattern));
    return result;
}

/* ── IFS split ────────────────────────────────────────────────────────────── */

WordList expand_split_ifs(ShellContext *ctx, const char *s) {
    WordList result; wordlist_init(&result);
    if (!s) return result;
    const char *ifs=shell_getenv(ctx,"IFS"); if(!ifs) ifs=" \t\n";
    SB tok={0};
    for (const char *p=s; *p; p++) {
        if (strchr(ifs,*p)) { if(tok.len>0){wordlist_push(&result,sb_take(&tok));tok=(SB){0};} }
        else sb_push(&tok,*p);
    }
    if (tok.len>0) wordlist_push(&result,sb_take(&tok)); else sb_free(&tok);
    return result;
}

/* ── Full word expansion pipeline ─────────────────────────────────────────── */

WordList expand_word(ShellContext *ctx, const char *word, bool glob_ok, bool split_ok) {
    WordList result; wordlist_init(&result);
    if (!word) return result;
    char *expanded = expand_string(ctx, word);
    if (word[0]=='~') { char *t=expand_tilde(ctx,expanded); str_free(expanded); expanded=t; }
    WordList brace = expand_brace(expanded); str_free(expanded);
    for (int bi=0; bi<brace.count; bi++) {
        char *bword=brace.words[bi];
        if (split_ok) {
            WordList split=expand_split_ifs(ctx,bword);
            for (int si=0; si<split.count; si++) {
                char *sword=split.words[si];
                if (glob_ok) {
                    WordList globs=expand_glob(sword);
                    for (int gi=0; gi<globs.count; gi++) wordlist_push(&result,globs.words[gi]);
                    globs.count=0; wordlist_free(&globs);
                } else wordlist_push(&result,str_dup(sword));
                str_free(split.words[si]); split.words[si]=NULL;
            }
            split.count=0; wordlist_free(&split);
        } else if (glob_ok) {
            WordList globs=expand_glob(bword);
            for (int gi=0; gi<globs.count; gi++) wordlist_push(&result,globs.words[gi]);
            globs.count=0; wordlist_free(&globs);
        } else wordlist_push(&result,str_dup(bword));
    }
    wordlist_free(&brace);
    if (result.count==0) wordlist_push(&result,str_dup(""));
    return result;
}