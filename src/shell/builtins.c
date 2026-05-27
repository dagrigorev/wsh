/*
 * builtins.c — Built-in command implementations.
 *
 * Output goes through ctx->io (IShellIO) — no direct handle writes.
 * The dispatch table maps names to functions; builtin_find() scans it linearly.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include "builtins.h"
#include "shell_ctx.h"
#include "expand.h"
#include "env.h"
#include "jobs.h"
#include "history.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"
#include "../ai/wsh_ai.h"
#include "../man_viewer.h"

/* ── Dispatch table (Open/Closed: add entries, never touch builtin_find) ───── */

typedef struct { const char *name; BuiltinFn fn; } BuiltinEntry;

/* All implementations are defined later in this file; declare them first */
int builtin_cd(int,char**,ShellContext*);
int builtin_echo(int,char**,ShellContext*);
int builtin_printf_cmd(int,char**,ShellContext*);
int builtin_print(int,char**,ShellContext*);
int builtin_export(int,char**,ShellContext*);
int builtin_unset(int,char**,ShellContext*);
int builtin_alias(int,char**,ShellContext*);
int builtin_unalias(int,char**,ShellContext*);
int builtin_source(int,char**,ShellContext*);
int builtin_exit(int,char**,ShellContext*);
int builtin_return_cmd(int,char**,ShellContext*);
int builtin_true_cmd(int,char**,ShellContext*);
int builtin_false_cmd(int,char**,ShellContext*);
int builtin_test(int,char**,ShellContext*);
int builtin_read(int,char**,ShellContext*);
int builtin_set_cmd(int,char**,ShellContext*);
int builtin_setopt(int,char**,ShellContext*);
int builtin_unsetopt(int,char**,ShellContext*);
int builtin_jobs(int,char**,ShellContext*);
int builtin_fg(int,char**,ShellContext*);
int builtin_bg(int,char**,ShellContext*);
int builtin_kill_cmd(int,char**,ShellContext*);
int builtin_wait_cmd(int,char**,ShellContext*);
int builtin_pwd(int,char**,ShellContext*);
int builtin_type(int,char**,ShellContext*);
int builtin_whence(int,char**,ShellContext*);
int builtin_which(int,char**,ShellContext*);
int builtin_command(int,char**,ShellContext*);
int builtin_eval(int,char**,ShellContext*);
int builtin_exec(int,char**,ShellContext*);
int builtin_local(int,char**,ShellContext*);
int builtin_typeset(int,char**,ShellContext*);
int builtin_hash(int,char**,ShellContext*);
int builtin_trap(int,char**,ShellContext*);
int builtin_open(int,char**,ShellContext*);
int builtin_clip(int,char**,ShellContext*);
int builtin_env_cmd(int,char**,ShellContext*);
int builtin_sudo(int,char**,ShellContext*);
int builtin_man(int,char**,ShellContext*);
int builtin_help(int,char**,ShellContext*);
int builtin_history(int,char**,ShellContext*);
int builtin_at(int,char**,ShellContext*);
int builtin_atq(int,char**,ShellContext*);
int builtin_atrm(int,char**,ShellContext*);
int builtin_noop(int,char**,ShellContext*);
int builtin_ai(int,char**,ShellContext*);

static const BuiltinEntry BUILTIN_TABLE[] = {
    { "cd",       builtin_cd          },
    { "echo",     builtin_echo        },
    { "printf",   builtin_printf_cmd  },
    { "print",    builtin_print       },
    { "export",   builtin_export      },
    { "unset",    builtin_unset       },
    { "alias",    builtin_alias       },
    { "unalias",  builtin_unalias     },
    { "source",   builtin_source      },
    { ".",        builtin_source      },
    { "exit",     builtin_exit        },
    { "return",   builtin_return_cmd  },
    { "true",     builtin_true_cmd    },
    { "false",    builtin_false_cmd   },
    { "test",     builtin_test        },
    { "[",        builtin_test        },
    { "read",     builtin_read        },
    { "set",      builtin_set_cmd     },
    { "setopt",   builtin_setopt      },
    { "unsetopt", builtin_unsetopt    },
    { "shopt",    builtin_setopt      },
    { "jobs",     builtin_jobs        },
    { "fg",       builtin_fg          },
    { "bg",       builtin_bg          },
    { "kill",     builtin_kill_cmd    },
    { "wait",     builtin_wait_cmd    },
    { "pwd",      builtin_pwd         },
    { "type",     builtin_type        },
    { "whence",   builtin_whence      },
    { "where",    builtin_whence      },
    { "which",    builtin_which       },
    { "command",  builtin_command     },
    { "eval",     builtin_eval        },
    { "exec",     builtin_exec        },
    { "local",    builtin_local       },
    { "typeset",  builtin_typeset     },
    { "declare",  builtin_typeset     },
    { "hash",     builtin_hash        },
    { "trap",     builtin_trap        },
    { "open",     builtin_open        },
    { "clip",     builtin_clip        },
    { "env",      builtin_env_cmd     },
    { "sudo",     builtin_sudo        },
    { "man",      builtin_man         },
    { "help",     builtin_help        },
    { "history",  builtin_history     },
    { "at",       builtin_at          },
    { "atq",      builtin_atq         },
    { "atrm",     builtin_atrm        },
    { "umask",    builtin_noop        },
    { "ulimit",   builtin_noop        },
    { "autoload", builtin_noop        },
    { "compdef",  builtin_noop        },
    { "compctl",  builtin_noop        },
    { "compinit", builtin_noop        },
    { "zstyle",   builtin_noop        },
    { "zle",      builtin_noop        },
    { "ai",       builtin_ai          },
    { NULL,       NULL                }
};

BuiltinFn builtin_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; BUILTIN_TABLE[i].name; i++)
        if (strcmp(BUILTIN_TABLE[i].name, name) == 0) return BUILTIN_TABLE[i].fn;
    return NULL;
}

/* ── Output helpers ────────────────────────────────────────────────────────── */

static void out(ShellContext *ctx, const char *s)       { if (s) io_write(ctx->io, s); }
static void outln(ShellContext *ctx, const char *s)     { if (s) io_writeln(ctx->io, s); }
static void outfmt(ShellContext *ctx, const char *fmt, ...) {
    char buf[1024]; va_list ap;
    va_start(ap, fmt); _vsnprintf(buf, sizeof(buf)-1, fmt, ap); va_end(ap);
    buf[sizeof(buf)-1] = '\0';
    io_write(ctx->io, buf);
}

/* ── cd ─────────────────────────────────────────────────────────────────────── */

int builtin_cd(int argc, char **argv, ShellContext *ctx) {
    const char *dir = NULL;
    if (argc < 2) {
        dir = shell_getenv(ctx, "HOME");
        if (!dir) dir = shell_getenv(ctx, "USERPROFILE");
        if (!dir) dir = "C:\\";
    } else if (!strcmp(argv[1], "-")) {
        dir = shell_getenv(ctx, "OLDPWD");
        if (!dir) { outln(ctx, "cd: OLDPWD not set"); return 1; }
        outln(ctx, dir);
    } else {
        dir = argv[1];
    }
    char *expanded = expand_tilde(ctx, dir);
    shell_setenv(ctx, "OLDPWD", ctx->cwd, true);
    wchar_t *wdir = u8_to_u16(expanded, NULL);
    BOOL ok = wdir && SetCurrentDirectoryW(wdir);
    str_free(wdir); str_free(expanded);
    if (!ok) { outfmt(ctx, "cd: %s: No such file or directory\r\n", dir); return 1; }
    wchar_t wcwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, wcwd);
    char *ncwd = u16_to_u8(wcwd, NULL);
    if (ncwd) { strncpy(ctx->cwd, ncwd, MAX_PATH-1); shell_setenv(ctx, "PWD", ncwd, true); str_free(ncwd); }
    return 0;
}

/* ── echo ───────────────────────────────────────────────────────────────────── */

int builtin_echo(int argc, char **argv, ShellContext *ctx) {
    int no_nl = 0, interp = 0, start = 1;
    while (start < argc && argv[start][0] == '-') {
        int valid = 1; const char *p;
        for (p = argv[start]+1; *p; p++) {
            if (*p=='n') no_nl=1; else if (*p=='e') interp=1; else if (*p=='E') interp=0; else { valid=0; break; }
        }
        if (!valid) break; start++;
    }
    for (int i = start; i < argc; i++) {
        if (i > start) out(ctx, " ");
        if (interp) {
            for (const char *p = argv[i]; *p; p++) {
                if (*p=='\\' && p[1]) {
                    p++; switch (*p) {
                        case 'n': out(ctx,"\n"); break; case 'r': out(ctx,"\r"); break;
                        case 't': out(ctx,"\t"); break; case '\\': out(ctx,"\\"); break;
                        case 'e': out(ctx,"\x1B"); break;
                        default: { char cs[3]={'\\',*p,0}; out(ctx,cs); break; }
                    }
                } else { char cs[2]={*p,0}; out(ctx,cs); }
            }
        } else { out(ctx, argv[i]); }
    }
    if (!no_nl) out(ctx, "\r\n");
    return 0;
}

/* ── printf ─────────────────────────────────────────────────────────────────── */

int builtin_printf_cmd(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { outln(ctx, "printf: missing format"); return 1; }
    const char *fmt = argv[1];
    int nargs = argc - 2;

    /* Count format specifiers (excluding %%) to enable format-string recycling */
    int nspec = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p == '%' && p[1] && p[1] != '%') nspec++;
    }
    if (nspec == 0 && nargs > 0) nspec = 1;

    int arg_off = 2; /* first value argument index */
    while (arg_off < argc) {
        char buf[4096];
        int remaining = argc - arg_off;
        switch (remaining) {
            case 0: _snprintf(buf, sizeof(buf), "%s", fmt); break;
            case 1: _snprintf(buf, sizeof(buf), fmt, argv[arg_off]); break;
            case 2: _snprintf(buf, sizeof(buf), fmt, argv[arg_off], argv[arg_off+1]); break;
            case 3: _snprintf(buf, sizeof(buf), fmt, argv[arg_off], argv[arg_off+1], argv[arg_off+2]); break;
            default: _snprintf(buf, sizeof(buf), fmt, argv[arg_off], argv[arg_off+1], argv[arg_off+2], argv[arg_off+3]); break;
        }
        buf[sizeof(buf)-1] = '\0';
        out(ctx, buf);
        arg_off += nspec;
    }
    return 0;
}

/* ── print (zsh-compatible practical subset) ───────────────────────────────── */

int builtin_print(int argc, char **argv, ShellContext *ctx) {
    bool no_newline = false;
    bool raw = false;
    int start = 1;
    for (; start < argc && argv[start][0] == '-'; start++) {
        if (!strcmp(argv[start], "-n")) no_newline = true;
        else if (!strcmp(argv[start], "-r")) raw = true;
        else if (!strcmp(argv[start], "--")) { start++; break; }
        else break;
    }
    (void)raw; /* lexer already preserves literal backslashes for this subset */
    for (int i = start; i < argc; i++) {
        if (i > start) out(ctx, " ");
        out(ctx, argv[i]);
    }
    if (!no_newline) out(ctx, "\r\n");
    return 0;
}

/* ── export ─────────────────────────────────────────────────────────────────── */

int builtin_export(int argc, char **argv, ShellContext *ctx) {
    if (argc == 1) {
        for (EnvScope *s = ctx->env; s; s = s->parent)
            for (EnvVar *v = s->vars; v; v = v->next)
                if (v->exported) outfmt(ctx,"export %s=\"%s\"\r\n",v->name,v->value?v->value:"");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i],'=');
        if (eq) {
            char name[256]={0}; size_t nl=(size_t)(eq-argv[i]); if(nl>255)nl=255;
            strncpy(name,argv[i],nl); env_set(ctx->env,name,eq+1,true);
        } else {
            const char *val=shell_getenv(ctx,argv[i]); env_set(ctx->env,argv[i],val?val:"",true);
        }
    }
    return 0;
}

/* ── unset ───────────────────────────────────────────────────────────────────── */

int builtin_unset(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) shell_unsetenv(ctx, argv[i]); return 0;
}

/* ── alias / unalias ─────────────────────────────────────────────────────────── */

int builtin_alias(int argc, char **argv, ShellContext *ctx) {
    if (argc==1) {
        for (Alias *a=ctx->aliases; a; a=a->next) outfmt(ctx,"alias %s='%s'\r\n",a->name,a->value);
        return 0;
    }
    for (int i=1; i<argc; i++) {
        char *eq=strchr(argv[i],'=');
        if (!eq) {
            for (Alias *a=ctx->aliases; a; a=a->next)
                if (!strcmp(a->name,argv[i])) { outfmt(ctx,"alias %s='%s'\r\n",a->name,a->value); break; }
            continue;
        }
        char name[256]={0}; size_t nl=(size_t)(eq-argv[i]); if(nl>255)nl=255; strncpy(name,argv[i],nl);
        const char *val=eq+1; size_t vlen=strlen(val); char *clean;
        if (vlen>=2&&((val[0]=='\''&&val[vlen-1]=='\'')||(val[0]=='"'&&val[vlen-1]=='"')))
            clean=str_ndup(val+1,vlen-2);
        else clean=str_dup(val);
        for (Alias *a=ctx->aliases; a; a=a->next)
            if (!strcmp(a->name,name)) { str_free(a->value); a->value=clean; clean=NULL; break; }
        if (clean) {
            Alias *na=(Alias*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Alias));
            na->name=str_dup(name); na->value=clean; na->next=ctx->aliases; ctx->aliases=na;
        }
    }
    return 0;
}

int builtin_unalias(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        Alias **pp=&ctx->aliases;
        while (*pp) {
            if (!strcmp((*pp)->name,argv[i])) {
                Alias *d=*pp; *pp=d->next; str_free(d->name); str_free(d->value);
                HeapFree(GetProcessHeap(),0,d); break;
            }
            pp=&(*pp)->next;
        }
    }
    return 0;
}

/* ── source ──────────────────────────────────────────────────────────────────── */

int builtin_source(int argc, char **argv, ShellContext *ctx) {
    if (argc<2) { outln(ctx,"source: filename required"); return 1; }
    char *path=expand_tilde(ctx,argv[1]); int ret=shell_source(ctx,path); str_free(path); return ret;
}

/* ── exit / return ───────────────────────────────────────────────────────────── */

int builtin_exit(int argc, char **argv, ShellContext *ctx) {
    int code=argc>1?atoi(argv[1]):ctx->last_status;
    ctx->exit_requested=true; ctx->exit_code=code; return code;
}
int builtin_return_cmd(int argc, char **argv, ShellContext *ctx) {
    int code=argc>1?atoi(argv[1]):ctx->last_status;
    ctx->return_requested=true; ctx->return_code=code; ctx->last_status=code; return code;
}

/* ── true / false ────────────────────────────────────────────────────────────── */

int builtin_true_cmd(int argc, char **argv, ShellContext *ctx)  { (void)argc;(void)argv;(void)ctx; return 0; }
int builtin_false_cmd(int argc, char **argv, ShellContext *ctx) { (void)argc;(void)argv;(void)ctx; return 1; }

/* ── test / [ ────────────────────────────────────────────────────────────────── */

/* Forward declaration for recursive test evaluator */
static int test_eval(char **argv, int n, int *pos, ShellContext *ctx);

/* Evaluate a primary test expression: unary flag, binary comparison, or string */
static int test_primary(char **argv, int n, int *pos, ShellContext *ctx) {
    if (*pos >= n) return 1;
    /* Handle '(' grouping */
    if (!strcmp(argv[*pos], "(")) {
        (*pos)++;
        int ret = test_eval(argv, n, pos, ctx);
        if (*pos < n && !strcmp(argv[*pos], ")")) (*pos)++;
        return ret;
    }
    /* Handle '!' negation */
    if (!strcmp(argv[*pos], "!")) {
        (*pos)++;
        return !test_primary(argv, n, pos, ctx);
    }
    /* Unary operator: -flag arg */
    if (*pos + 2 <= n && argv[*pos][0] == '-' && argv[*pos][2] == '\0') {
        const char *f = argv[*pos + 1]; *pos += 2;
        switch (argv[*pos - 2][1]) {
            case 'z': return strlen(f)==0?0:1;
            case 'n': return strlen(f)!=0?0:1;
            case 'f': return (path_exists(f)&&!path_is_dir(f))?0:1;
            case 'd': return path_is_dir(f)?0:1;
            case 'e': return path_exists(f)?0:1;
            case 'r': case 'w': case 'x': return path_exists(f)?0:1;
            case 's': { wchar_t *w=u8_to_u16(f,NULL); WIN32_FILE_ATTRIBUTE_DATA fa={0};
                        BOOL ok=w&&GetFileAttributesExW(w,GetFileExInfoStandard,&fa); str_free(w);
                        return (ok&&(fa.nFileSizeLow>0||fa.nFileSizeHigh>0))?0:1; }
            default: return 1;
        }
    }
    /* Binary comparison: a op b */
    if (*pos + 3 <= n) {
        const char *a = argv[*pos], *op = argv[*pos + 1], *b = argv[*pos + 2];
        if (!strcmp(op,"=")||!strcmp(op,"==")) { *pos+=3; return strcmp(a,b)==0?0:1; }
        if (!strcmp(op,"!="))                  { *pos+=3; return strcmp(a,b)!=0?0:1; }
        if (!strcmp(op,"<"))                   { *pos+=3; return strcmp(a,b)<0?0:1; }
        if (!strcmp(op,">"))                   { *pos+=3; return strcmp(a,b)>0?0:1; }
        if (!strcmp(op,"-eq")) { *pos+=3; return atoi(a)==atoi(b)?0:1; }
        if (!strcmp(op,"-ne")) { *pos+=3; return atoi(a)!=atoi(b)?0:1; }
        if (!strcmp(op,"-lt")) { *pos+=3; return atoi(a)< atoi(b)?0:1; }
        if (!strcmp(op,"-le")) { *pos+=3; return atoi(a)<=atoi(b)?0:1; }
        if (!strcmp(op,"-gt")) { *pos+=3; return atoi(a)> atoi(b)?0:1; }
        if (!strcmp(op,"-ge")) { *pos+=3; return atoi(a)>=atoi(b)?0:1; }
    }
    /* Simple string test */
    const char *s = argv[*pos]; (*pos)++;
    return s[0]!='\0'?0:1;
}

/* Recursive test expression: handles -a (AND) with higher precedence than -o (OR) */
static int test_eval(char **argv, int n, int *pos, ShellContext *ctx) {
    /* Term: primary ( -a primary )* */
    int left = test_primary(argv, n, pos, ctx);
    while (*pos < n && !strcmp(argv[*pos], "-a")) { (*pos)++; int r = test_primary(argv, n, pos, ctx); left = (left==0||r==0)?1:0; }
    return left;
}

int builtin_test(int argc, char **argv, ShellContext *ctx) {
    (void)ctx;
    int n=argc; if (n>1&&!strcmp(argv[n-1],"]")) n--;
    if (n<2) return 1;
    int pos = 1;
    /* Top-level uses -o (OR) */
    int left = test_eval(argv, n, &pos, ctx);
    while (pos < n && !strcmp(argv[pos], "-o")) { pos++; int r = test_eval(argv, n, &pos, ctx); left = (left==0&&r==0)?1:0; }
    return left;
}

/* ── read ────────────────────────────────────────────────────────────────────── */

int builtin_read(int argc, char **argv, ShellContext *ctx) {
    int raw=0; const char *prompt=NULL;
    const char *varnames[64]; int nvar = 0;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"-r")) raw=1;
        else if (!strcmp(argv[i],"-p")&&i+1<argc) prompt=argv[++i];
        else if (nvar < 63) varnames[nvar++] = argv[i];
    }
    if (nvar == 0) varnames[nvar++] = "REPLY";
    if (prompt) out(ctx,prompt);
    char buf[4096]={0}; (void)raw;
    int nread = ctx->io->read_line(ctx->io, buf, (int)sizeof(buf)-1);
    if (nread <= 0) return 1;
    buf[nread] = '\0';
    str_trim(buf);
    /* Split by IFS and assign fields */
    const char *ifs = shell_getenv(ctx, "IFS");
    if (!ifs) ifs = " \t\n";
    char *fields[64]; int nfield = 0;
    char *p = buf;
    while (*p && nfield < 63) {
        while (*p && strchr(ifs, *p)) p++;
        if (!*p) break;
        fields[nfield++] = p;
        while (*p && !strchr(ifs, *p)) p++;
        if (*p) { *p = '\0'; p++; }
    }
    for (int i = 0; i < nvar; i++) {
        if (i < nvar - 1 && i < nfield)
            shell_setenv(ctx, varnames[i], fields[i], false);
        else if (i == nvar - 1) {
            /* Last variable gets the remainder (re-join remaining fields) */
            if (i < nfield) {
                char *rem = fields[i];
                for (int j = i + 1; j < nfield; j++) {
                    size_t rlen = strlen(rem);
                    char *joined = (char *)HeapAlloc(GetProcessHeap(), 0, rlen + strlen(fields[j]) + 2);
                    if (joined) { memcpy(joined, rem, rlen); joined[rlen] = ' '; memcpy(joined+rlen+1, fields[j], strlen(fields[j])+1); }
                    shell_setenv(ctx, varnames[i], joined, false);
                    HeapFree(GetProcessHeap(), 0, joined);
                }
            } else {
                shell_setenv(ctx, varnames[i], "", false);
            }
        }
    }
    return 0;
}

/* ── set ─────────────────────────────────────────────────────────────────────── */

int builtin_set_cmd(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        if (argv[i][0]!='-') continue;
        for (const char *p=argv[i]+1; *p; p++) {
            if (*p=='e') ctx->opts.err_exit=1;
            else if (*p=='x') ctx->opts.xtrace=1;
            else if (*p=='u') ctx->opts.nounset=1;
        }
    }
    return 0;
}

/* ── setopt ──────────────────────────────────────────────────────────────────── */

static void print_option_if(ShellContext *ctx, const char *name, int enabled) {
    if (enabled) outfmt(ctx, "%s\r\n", name);
}

static int set_one_option(ShellContext *ctx, const char *opt, int val) {
    if      (!_stricmp(opt,"AUTO_CD") || !_stricmp(opt,"AUTOCD")) ctx->opts.auto_cd=val;
    else if (!_stricmp(opt,"CORRECT"))                             ctx->opts.correct=val;
    else if (!_stricmp(opt,"GLOB_STAR") || !_stricmp(opt,"GLOBSTAR") ||
             !_stricmp(opt,"GLOB_STAR_SHORT"))                    ctx->opts.glob_star=val;
    else if (!_stricmp(opt,"HIST_IGNORE_DUPS") || !_stricmp(opt,"HISTIGNOREDUPS")) ctx->opts.hist_ignore_dups=val;
    else if (!_stricmp(opt,"SHARE_HISTORY") || !_stricmp(opt,"SHAREHISTORY"))       ctx->opts.share_history=val;
    else if (!_stricmp(opt,"NO_CLOBBER") || !_stricmp(opt,"NOCLOBBER"))             ctx->opts.no_clobber=val;
    else if (!_stricmp(opt,"ERR_EXIT") || !_stricmp(opt,"ERREXIT"))                 ctx->opts.err_exit=val;
    else if (!_stricmp(opt,"XTRACE"))                                                ctx->opts.xtrace=val;
    else if (!_stricmp(opt,"NOUNSET"))                                               ctx->opts.nounset=val;
    else return 0;
    return 1;
}

int builtin_setopt(int argc, char **argv, ShellContext *ctx) {
    if (argc == 1) {
        print_option_if(ctx, "AUTO_CD", ctx->opts.auto_cd);
        print_option_if(ctx, "CORRECT", ctx->opts.correct);
        print_option_if(ctx, "GLOB_STAR", ctx->opts.glob_star);
        print_option_if(ctx, "HIST_IGNORE_DUPS", ctx->opts.hist_ignore_dups);
        print_option_if(ctx, "SHARE_HISTORY", ctx->opts.share_history);
        print_option_if(ctx, "NO_CLOBBER", ctx->opts.no_clobber);
        print_option_if(ctx, "ERR_EXIT", ctx->opts.err_exit);
        print_option_if(ctx, "XTRACE", ctx->opts.xtrace);
        print_option_if(ctx, "NOUNSET", ctx->opts.nounset);
        return 0;
    }
    int rc = 0;
    for (int i=1; i<argc; i++) {
        const char *opt=argv[i]; int unset=0;
        if (str_startswith(opt,"NO_")||str_startswith(opt,"no_")) { unset=1; opt+=3; }
        if (!set_one_option(ctx, opt, !unset)) { outfmt(ctx, "setopt: no such option: %s\r\n", argv[i]); rc = 1; }
    }
    return rc;
}

int builtin_unsetopt(int argc, char **argv, ShellContext *ctx) {
    int rc = 0;
    for (int i=1; i<argc; i++) {
        if (!set_one_option(ctx, argv[i], 0)) { outfmt(ctx, "unsetopt: no such option: %s\r\n", argv[i]); rc = 1; }
    }
    return rc;
}

/* ── jobs / fg / bg ──────────────────────────────────────────────────────────── */

int builtin_jobs(int argc, char **argv, ShellContext *ctx) {
    (void)argc;(void)argv; job_poll_all(&ctx->jobs); job_print_all(&ctx->jobs, ctx->io); return 0;
}
int builtin_fg(int argc, char **argv, ShellContext *ctx) {
    int id=0;
    if (argc>1) id=atoi(argv[1][0]=='%'?argv[1]+1:argv[1]);
    if (!id) for (int i=JOBS_MAX-1;i>=0;i--) if (ctx->jobs.jobs[i].id){id=ctx->jobs.jobs[i].id;break;}
    if (!id) { outln(ctx,"fg: no current job"); return 1; }
    return job_fg(&ctx->jobs,id,ctx->io);
}
int builtin_bg(int argc, char **argv, ShellContext *ctx) {
    int id=argc>1?atoi(argv[1][0]=='%'?argv[1]+1:argv[1]):1;
    return job_bg(&ctx->jobs,id,ctx->io)?0:1;
}

/* ── kill ────────────────────────────────────────────────────────────────────── */

int builtin_kill_cmd(int argc, char **argv, ShellContext *ctx) {
    int sig=15, start=1;
    if (argc>1&&argv[1][0]=='-') { sig=atoi(argv[1]+1); start=2; }
    for (int i=start; i<argc; i++) {
        if (argv[i][0]=='%') { job_kill(&ctx->jobs,atoi(argv[i]+1),sig); }
        else { DWORD pid=(DWORD)atoi(argv[i]); HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid);
               if(h){TerminateProcess(h,(UINT)sig);CloseHandle(h);} }
    }
    return 0;
}

/* ── wait ────────────────────────────────────────────────────────────────────── */

int builtin_wait_cmd(int argc, char **argv, ShellContext *ctx) {
    if (argc>1) {
        int id=atoi(argv[1][0]=='%'?argv[1]+1:argv[1]);
        Job *j=job_find(&ctx->jobs,id);
        if (j&&j->hprocess) WaitForSingleObject(j->hprocess,INFINITE);
    } else {
        for (int i=0;i<JOBS_MAX;i++) { Job *j=&ctx->jobs.jobs[i];
            if (j->id&&j->hprocess) WaitForSingleObject(j->hprocess,INFINITE); }
    }
    job_poll_all(&ctx->jobs); return 0;
}

/* ── pwd ─────────────────────────────────────────────────────────────────────── */

int builtin_pwd(int argc, char **argv, ShellContext *ctx) { (void)argc;(void)argv; outln(ctx,ctx->cwd); return 0; }

/* ── type ────────────────────────────────────────────────────────────────────── */

int builtin_type(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        if (builtin_find(argv[i])) { outfmt(ctx,"%s is a shell builtin\r\n",argv[i]); continue; }
        int found=0;
        for (Alias *a=ctx->aliases; a; a=a->next)
            if (!strcmp(a->name,argv[i])) { outfmt(ctx,"%s is an alias for '%s'\r\n",argv[i],a->value); found=1; break; }
        if (!found) {
            char *p=shell_which(ctx,argv[i]);
            if (p) { outfmt(ctx,"%s is %s\r\n",argv[i],p); str_free(p); }
            else     outfmt(ctx,"%s not found\r\n",argv[i]);
        }
    }
    return 0;
}

int builtin_whence(int argc, char **argv, ShellContext *ctx) {
    bool verbose = false;
    int start = 1;
    if (argc > 1 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "-w"))) {
        verbose = true;
        start = 2;
    }
    for (int i=start; i<argc; i++) {
        int found = 0;
        for (Alias *a=ctx->aliases; a; a=a->next) {
            if (!strcmp(a->name, argv[i])) {
                if (verbose) outfmt(ctx, "%s: alias for %s\r\n", argv[i], a->value);
                else outln(ctx, a->value);
                found = 1;
                break;
            }
        }
        if (found) continue;
        if (builtin_find(argv[i])) { if (verbose) outfmt(ctx, "%s: shell builtin\r\n", argv[i]); else outln(ctx, argv[i]); continue; }
        char *p=shell_which(ctx,argv[i]);
        if (p) { outln(ctx,p); str_free(p); }
        else { if (verbose) outfmt(ctx, "%s: not found\r\n", argv[i]); }
    }
    return 0;
}

/* ── which ───────────────────────────────────────────────────────────────────── */

int builtin_which(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        char *p=shell_which(ctx,argv[i]);
        if (p) { outln(ctx,p); str_free(p); } else outfmt(ctx,"%s not found\r\n",argv[i]);
    }
    return 0;
}

/* ── command ────────────────────────────────────────────────────────────────── */

int builtin_command(int argc, char **argv, ShellContext *ctx) {
    /* zsh-compatible basics:
     *   command -v name  -> identify a command without running aliases
     *   command name ... -> execute while suppressing alias expansion
     * This prevents aliases such as `alias ls="ls"` from recursing forever and
     * gives users an explicit escape hatch, just like zsh. */
    if (argc >= 3 && strcmp(argv[1], "-v") == 0) {
        if (builtin_find(argv[2])) { outln(ctx, argv[2]); return 0; }
        char *p = shell_which(ctx, argv[2]);
        if (p) { outln(ctx, p); str_free(p); return 0; }
        return 1;
    }
    if (argc >= 2) {
        char *line = str_join(argv + 1, argc - 1, " ");
        bool saved = ctx->suppress_alias;
        ctx->suppress_alias = true;
        int ret = shell_exec_line(ctx, line);
        ctx->suppress_alias = saved;
        str_free(line);
        return ret;
    }
    return 0;
}

/* ── eval ────────────────────────────────────────────────────────────────────── */

int builtin_eval(int argc, char **argv, ShellContext *ctx) {
    if (argc<2) return 0;
    char *line=str_join(argv+1,argc-1," "); int ret=shell_exec_line(ctx,line); str_free(line); return ret;
}

/* ── exec ────────────────────────────────────────────────────────────────────── */

int builtin_exec(int argc, char **argv, ShellContext *ctx) {
    if (argc<2) return 0;
    char *line=str_join(argv+1,argc-1," "); int ret=shell_exec_line(ctx,line); str_free(line);
    ctx->exit_requested=true; ctx->exit_code=ret; return ret;
}

/* ── local / typeset ──────────────────────────────────────────────────────────── */

int builtin_local(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        char *eq=strchr(argv[i],'=');
        if (eq) { char name[256]={0}; size_t nl=(size_t)(eq-argv[i]); if(nl>255)nl=255; strncpy(name,argv[i],nl);
                  env_set(ctx->env,name,eq+1,false); }
        else env_set(ctx->env,argv[i],"",false);
    }
    return 0;
}

int builtin_typeset(int argc, char **argv, ShellContext *ctx) {
    int do_export=0, do_int=0, start=1;
    for (; start<argc&&argv[start][0]=='-'; start++)
        for (const char *p=argv[start]+1; *p; p++) { if(*p=='x')do_export=1; if(*p=='i')do_int=1; }
    for (int i=start; i<argc; i++) {
        char *eq=strchr(argv[i],'=');
        if (eq) {
            char name[256]={0}; size_t nl=(size_t)(eq-argv[i]); if(nl>255)nl=255; strncpy(name,argv[i],nl);
            if (do_int) { long v=expand_arith(ctx,eq+1); char buf[32]; _snprintf(buf,sizeof(buf),"%ld",v);
                          env_set(ctx->env,name,buf,do_export?true:false); }
            else          env_set(ctx->env,name,eq+1,do_export?true:false);
        } else { const char *cur=shell_getenv(ctx,argv[i]); env_set(ctx->env,argv[i],cur?cur:"",do_export?true:false); }
    }
    return 0;
}

/* ── hash / trap ─────────────────────────────────────────────────────────────── */

int builtin_hash(int argc, char **argv, ShellContext *ctx)  { (void)argc;(void)argv;(void)ctx; return 0; }
int builtin_trap(int argc, char **argv, ShellContext *ctx)  { (void)argc;(void)argv;(void)ctx; return 0; }

/* ── open ────────────────────────────────────────────────────────────────────── */

int builtin_open(int argc, char **argv, ShellContext *ctx) {
    if (argc<2) { outln(ctx,"open: filename required"); return 1; }
    wchar_t *wf=u8_to_u16(argv[1],NULL); if(!wf) return 1;
    HINSTANCE hi=ShellExecuteW(NULL,L"open",wf,NULL,NULL,SW_SHOWNORMAL);
    str_free(wf); return ((INT_PTR)hi>32)?0:1;
}

/* ── clip ────────────────────────────────────────────────────────────────────── */

int builtin_clip(int argc, char **argv, ShellContext *ctx) {
    (void)argc;(void)argv;
    char buf[65536]={0}; int n=ctx->io->read_line(ctx->io,buf,(int)sizeof(buf)-1); if(n<0)n=0; buf[n]='\0';
    if (OpenClipboard(NULL)) {
        EmptyClipboard(); size_t len=strlen(buf)+1;
        HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,len);
        if (hg) { char *p=(char*)GlobalLock(hg); if(p){memcpy(p,buf,len);GlobalUnlock(hg);} SetClipboardData(CF_TEXT,hg); }
        CloseClipboard();
    }
    return 0;
}

/* ── env ─────────────────────────────────────────────────────────────────────── */

int builtin_env_cmd(int argc, char **argv, ShellContext *ctx) {
    (void)argc;(void)argv;
    wchar_t *block=GetEnvironmentStringsW(); if(!block) return 1;
    for (const wchar_t *p=block; *p; p+=wcslen(p)+1) {
        char *line=u16_to_u8(p,NULL); if(line){outln(ctx,line);str_free(line);}
    }
    FreeEnvironmentStringsW(block); return 0;
}

/* ── sudo ────────────────────────────────────────────────────────────────────── */

int builtin_sudo(int argc, char **argv, ShellContext *ctx) {
    if (argc<2) { outln(ctx,"sudo: command required"); return 1; }
    char *cmd=str_join(argv+1,argc-1," "); wchar_t *wcmd=u8_to_u16(cmd,NULL); str_free(cmd);
    if (!wcmd) return 1;
    HINSTANCE hi=ShellExecuteW(NULL,L"runas",wcmd,NULL,NULL,SW_SHOWNORMAL);
    str_free(wcmd); return ((INT_PTR)hi>32)?0:1;
}


/* ── man / help ─────────────────────────────────────────────────────────────── */

static void exe_dir(char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    GetModuleFileNameA(NULL, out, (DWORD)out_size);
    char *last_bs = strrchr(out, '\\');
    if (last_bs) *last_bs = '\0';
}

static int print_text_file(ShellContext *ctx, const char *path) {
    wchar_t *wpath = u8_to_u16(path, NULL);
    if (!wpath) return 0;
    FILE *f = _wfopen(wpath, L"rb");
    str_free(wpath);
    if (!f) return 0;

    char buf[2048];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (ctx->io && ctx->io->write) ctx->io->write(ctx->io, buf, (int)n);
    }
    fclose(f);
    return 1;
}

static int print_man_topic(ShellContext *ctx, const char *topic) {
    if (!topic || !*topic) topic = "wsh";

    char path[MAX_PATH];
    char dir[MAX_PATH];
    exe_dir(dir, MAX_PATH);
    _snprintf(path, MAX_PATH, "%s\\man\\%s.txt", dir, topic);
    if (print_text_file(ctx, path)) return 0;

    char appdata[MAX_PATH] = {0};
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    _snprintf(path, MAX_PATH, "%s\\Wsh\\man\\%s.txt", appdata, topic);
    if (print_text_file(ctx, path)) return 0;

    outfmt(ctx, "man: no manual entry for %s\r\n", topic);
    outln(ctx, "Try: man wsh, man ls, man md, man tree, man wshinit, help");
    return 1;
}

int builtin_man(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { if (man_viewer_open_topic("wsh")) return 0; return print_man_topic(ctx, "wsh"); }
    if (man_viewer_open_topic(argv[1])) return 0;
    int ret = 0;
    for (int i = 1; i < argc; i++) if (print_man_topic(ctx, argv[i]) != 0) ret = 1;
    return ret;
}

int builtin_help(int argc, char **argv, ShellContext *ctx) {
    if (argc > 1) { if (man_viewer_open_topic(argv[1])) return 0; return print_man_topic(ctx, argv[1]); }
    outln(ctx, "Wsh built-ins:");
    outln(ctx, "  cd pwd echo printf export unset alias unalias source exit return");
    outln(ctx, "  set setopt jobs fg bg kill wait type which command eval exec");
    outln(ctx, "  open clip env sudo man help at atq atrm");
    outln(ctx, "Utilities distributed with Wsh:");
    outln(ctx, "  ls md tree wshinit");
    outln(ctx, "Use: man <topic> or <utility> --help");
    (void)argc; (void)argv;
    return 0;
}

/* ── noop ────────────────────────────────────────────────────────────────────── */

int builtin_noop(int argc, char **argv, ShellContext *ctx) { (void)argc;(void)argv;(void)ctx; return 0; }

/* -- history -------------------------------------------------------------------- */
int builtin_history(int argc, char **argv, ShellContext *ctx) {
    (void)argv;

    int count = history_count(&ctx->history);
    int display_count = count;
    int start_offset = 0;

    if (argc >= 2) {
        int n = atoi(argv[1]);
        if (n > 0 && n < count) {
            start_offset = count - n;
            display_count = n;
        }
    }

    /* history_at(0) = most recent, history_at(count-1) = oldest.
       We print oldest first with smallest number. */
    for (int i = 0; i < display_count; ++i) {
        int idx = count - 1 - start_offset - i;
        if (idx < 0 || idx >= count) continue;
        const char *item = history_at(&ctx->history, idx);
        if (!item) continue;

        char line[4096];
        _snprintf(line, sizeof(line), "%5d  %s", start_offset + i + 1, item);
        outln(ctx, line);
    }

    return 0;
}

/* ── at — schedule a task ────────────────────────────────────────────────────── */

static DWORD parse_delay(const char *s) {
    if (!s || !s[0]) return 0;
    long n = 0;
    const char *p = s;
    if (*p == '+') p++;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    if (n <= 0) return 0;
    switch (*p) {
        case 's': case 'S': return (DWORD)n * 1000;
        case 'm': case 'M': return (DWORD)n * 60000;
        case 'h': case 'H': return (DWORD)n * 3600000;
        default:            return (DWORD)n * 1000; /* default seconds */
    }
}

int builtin_at(int argc, char **argv, ShellContext *ctx) {
    /* at -l  — list queue (same as atq) */
    if (argc == 2 && (!strcmp(argv[1], "-l") || !strcmp(argv[1], "--list"))) {
        return builtin_atq(argc, argv, ctx);
    }
    /* at -r <id> — remove task (same as atrm) */
    if (argc >= 3 && (!strcmp(argv[1], "-r") || !strcmp(argv[1], "--remove"))) {
        char *eargv[3] = { "atrm", argv[2], NULL };
        return builtin_atrm(2, eargv, ctx);
    }
    /* at — show usage */
    if (argc < 3) {
        outln(ctx, "Usage:");
        outln(ctx, "  at +N[s|m|h] <command>   schedule command after N seconds/minutes/hours");
        outln(ctx, "  at -l                    list scheduled tasks (atq)");
        outln(ctx, "  at -r <id>               remove scheduled task (atrm)");
        outln(ctx, "  atrm <id>                remove scheduled task");
        outln(ctx, "  atq                      list scheduled tasks");
        outln(ctx, "Examples:");
        outln(ctx, "  at +5s echo hello        run echo hello in 5 seconds");
        outln(ctx, "  at +2m ls -la            run ls -la in 2 minutes");
        outln(ctx, "  atq                      list all scheduled tasks");
        return 0;
    }

    DWORD delay = parse_delay(argv[1]);
    if (delay == 0) {
        outfmt(ctx, "at: invalid delay: %s (use +N[s|m|h])\r\n", argv[1]);
        return 1;
    }

    char *command = str_join(argv + 2, argc - 2, " ");
    if (!command) return 1;

    int id = scheduler_add(&ctx->scheduler, delay, command);
    if (id < 0) {
        outln(ctx, "at: failed to schedule task (queue full?)");
        str_free(command);
        return 1;
    }

    outfmt(ctx, "task #%d scheduled\r\n", id);
    str_free(command);
    return 0;
}

/* ── atq — list scheduled tasks ───────────────────────────────────────────────── */

int builtin_atq(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv;
    Scheduler *s = &ctx->scheduler;

    if (s->count == 0) {
        outln(ctx, "no scheduled tasks");
        return 0;
    }

    outfmt(ctx, "%-4s %-8s %-10s %s\r\n", "ID", "Due(s)", "Status", "Command");
    DWORD now = GetTickCount();

    for (int i = 0; i < SCHED_MAX; i++) {
        SchedTask *t = &s->tasks[i];
        if (!t->id) continue;

        int32_t remaining = (int32_t)(t->due_at - now);
        if (remaining < 0) remaining = 0;

        const char *status_str = "pending";
        if (t->status == SCHED_RUNNING) status_str = "running";
        else if (t->status == SCHED_DONE)   status_str = "done";
        else if (t->status == SCHED_FAILED) status_str = "failed";
        if (t->interval_ms > 0) status_str = "repeat";

        char time_str[32];
        if (remaining > 0) {
            _snprintf(time_str, sizeof(time_str), "%lus",
                      (unsigned long)(remaining / 1000));
        } else {
            _snprintf(time_str, sizeof(time_str), "now");
        }

        outfmt(ctx, "%-4d %-8s %-10s %s\r\n",
               t->id, time_str, status_str, t->label);
    }

    return 0;
}

/* ── atrm — remove a scheduled task ──────────────────────────────────────────── */

int builtin_atrm(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) {
        outln(ctx, "Usage: atrm <id>");
        outln(ctx, "       atrm -a    remove all scheduled tasks");
        return 1;
    }

    if (!strcmp(argv[1], "-a") || !strcmp(argv[1], "--all")) {
        scheduler_clear(&ctx->scheduler);
        outln(ctx, "all scheduled tasks removed");
        return 0;
    }

    int id = atoi(argv[1]);
    if (id <= 0) {
        outfmt(ctx, "atrm: invalid task id: %s\r\n", argv[1]);
        return 1;
    }

    if (scheduler_remove(&ctx->scheduler, id)) {
        outfmt(ctx, "task #%d removed\r\n", id);
        return 0;
    }

    outfmt(ctx, "atrm: task #%d not found\r\n", id);
    return 1;
}

/* ── ai — built-in AI assistant ──────────────────────────────────────────── */

int builtin_ai(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) {
        outln(ctx, "ai: expected subcommand: status, suggest, explain, fix, on, off");
        outln(ctx, "  ai commentary on/off              toggle command commentary");
        outln(ctx, "  ai commentary test <cmd...>       test commentary with a command");
        outln(ctx, "  ai phi4 status                    show Phi-4 model status");
        outln(ctx, "  ai phi4 reload                    reload Phi-4 runtime");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        wsh_ai_cmd_status(ctx);
    } else if (strcmp(argv[1], "suggest") == 0) {
        wsh_ai_cmd_suggest(ctx, argc - 2, argv + 2);
    } else if (strcmp(argv[1], "explain") == 0) {
        wsh_ai_cmd_explain(ctx);
    } else if (strcmp(argv[1], "fix") == 0) {
        wsh_ai_cmd_fix(ctx);
    } else if (strcmp(argv[1], "on") == 0) {
        ctx->ai_enabled = true;
        outln(ctx, "AI: enabled");
    } else if (strcmp(argv[1], "off") == 0) {
        ctx->ai_enabled = false;
        outln(ctx, "AI: disabled");
    } else if (strcmp(argv[1], "commentary") == 0) {
        if (argc < 3) {
            outfmt(ctx, "Commentary: %s\r\n",
                   wsh_ai_commentary_is_enabled(ctx) ? "enabled" : "disabled");
            outfmt(ctx, "Provider: %s\r\n", wsh_ai_commentary_provider_type(ctx));
            return 0;
        }
        if (strcmp(argv[2], "on") == 0) {
            wsh_ai_commentary_set_enabled(ctx, true);
            outln(ctx, "AI commentary: enabled");
        } else if (strcmp(argv[2], "off") == 0) {
            wsh_ai_commentary_set_enabled(ctx, false);
            outln(ctx, "AI commentary: disabled");
        } else if (strcmp(argv[2], "test") == 0 && argc >= 4) {
            /* Reconstruct command from remaining args */
            char cmd_buf[4096] = {0};
            for (int i = 3; i < argc; i++) {
                if (i > 3) strncat(cmd_buf, " ", sizeof(cmd_buf) - strlen(cmd_buf) - 1);
                strncat(cmd_buf, argv[i], sizeof(cmd_buf) - strlen(cmd_buf) - 1);
            }
            if (wsh_ai_commentary_test(ctx, cmd_buf)) {
                const char *cc = wsh_ai_try_get_commentary(ctx, cmd_buf);
                if (cc && cc[0]) {
                    outfmt(ctx, "%s\r\n", cc);
                } else {
                    outln(ctx, "No commentary generated.");
                }
            } else {
                outln(ctx, "No commentary generated.");
            }
        } else {
            outln(ctx, "Usage: ai commentary on|off|test <cmd...>");
            return 1;
        }
    } else if (strcmp(argv[1], "phi4") == 0) {
        if (argc < 3) {
            outln(ctx, "Usage: ai phi4 status|reload");
            return 1;
        }
        if (strcmp(argv[2], "status") == 0) {
            outfmt(ctx, "Phi-4 model: %s\r\n",
                   wsh_ai_phi4_model_loaded(ctx) ? "loaded" : "missing");
            const char *mp = wsh_ai_phi4_model_path(ctx);
            if (mp && mp[0]) {
                outfmt(ctx, "Model path: %s\r\n", mp);
            }
        } else if (strcmp(argv[2], "reload") == 0) {
            if (wsh_ai_phi4_reload(ctx)) {
                outln(ctx, "Phi-4 runtime reloaded.");
            } else {
                outln(ctx, "Phi-4 runtime reload failed.");
            }
        } else {
            outfmt(ctx, "ai: unknown phi4 subcommand: %s\r\n", argv[2]);
            return 1;
        }
    } else {
        outfmt(ctx, "ai: unknown subcommand: %s\r\n", argv[1]);
        return 1;
    }
    return 0;
}