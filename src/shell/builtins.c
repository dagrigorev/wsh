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
#include "builtins.h"
#include "shell_ctx.h"
#include "expand.h"
#include "env.h"
#include "jobs.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"

/* ── Dispatch table (Open/Closed: add entries, never touch builtin_find) ───── */

typedef struct { const char *name; BuiltinFn fn; } BuiltinEntry;

/* All implementations are defined later in this file; declare them first */
int builtin_cd(int,char**,ShellContext*);
int builtin_echo(int,char**,ShellContext*);
int builtin_printf_cmd(int,char**,ShellContext*);
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
int builtin_jobs(int,char**,ShellContext*);
int builtin_fg(int,char**,ShellContext*);
int builtin_bg(int,char**,ShellContext*);
int builtin_kill_cmd(int,char**,ShellContext*);
int builtin_wait_cmd(int,char**,ShellContext*);
int builtin_pwd(int,char**,ShellContext*);
int builtin_type(int,char**,ShellContext*);
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
int builtin_noop(int,char**,ShellContext*);

static const BuiltinEntry BUILTIN_TABLE[] = {
    { "cd",       builtin_cd          },
    { "echo",     builtin_echo        },
    { "printf",   builtin_printf_cmd  },
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
    { "shopt",    builtin_setopt      },
    { "jobs",     builtin_jobs        },
    { "fg",       builtin_fg          },
    { "bg",       builtin_bg          },
    { "kill",     builtin_kill_cmd    },
    { "wait",     builtin_wait_cmd    },
    { "pwd",      builtin_pwd         },
    { "type",     builtin_type        },
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
    { "umask",    builtin_noop        },
    { "ulimit",   builtin_noop        },
    { "autoload", builtin_noop        },
    { "compdef",  builtin_noop        },
    { "compctl",  builtin_noop        },
    { "compinit", builtin_noop        },
    { "zstyle",   builtin_noop        },
    { "zle",      builtin_noop        },
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
    char buf[4096]; const char *fmt = argv[1];
    switch (argc-2) {
        case 0: _snprintf(buf,sizeof(buf),fmt); break;
        case 1: _snprintf(buf,sizeof(buf),fmt,argv[2]); break;
        case 2: _snprintf(buf,sizeof(buf),fmt,argv[2],argv[3]); break;
        case 3: _snprintf(buf,sizeof(buf),fmt,argv[2],argv[3],argv[4]); break;
        default:_snprintf(buf,sizeof(buf),fmt,argv[2],argv[3],argv[4],argv[5]); break;
    }
    buf[sizeof(buf)-1]='\0'; out(ctx,buf); return 0;
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
    int code=argc>1?atoi(argv[1]):ctx->last_status; ctx->last_status=code; return code;
}

/* ── true / false ────────────────────────────────────────────────────────────── */

int builtin_true_cmd(int argc, char **argv, ShellContext *ctx)  { (void)argc;(void)argv;(void)ctx; return 0; }
int builtin_false_cmd(int argc, char **argv, ShellContext *ctx) { (void)argc;(void)argv;(void)ctx; return 1; }

/* ── test / [ ────────────────────────────────────────────────────────────────── */

int builtin_test(int argc, char **argv, ShellContext *ctx) {
    (void)ctx;
    int n=argc; if (n>1&&!strcmp(argv[n-1],"]")) n--;
    if (n<2) return 1;
    if (n==3&&argv[1][0]=='-'&&argv[1][2]=='\0') {
        const char *f=argv[2]; switch(argv[1][1]) {
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
    if (n==4) {
        const char *a=argv[1],*op=argv[2],*b=argv[3];
        if (!strcmp(op,"=")||!strcmp(op,"==")) return strcmp(a,b)==0?0:1;
        if (!strcmp(op,"!="))                  return strcmp(a,b)!=0?0:1;
        if (!strcmp(op,"<"))                   return strcmp(a,b)<0?0:1;
        if (!strcmp(op,">"))                   return strcmp(a,b)>0?0:1;
        if (!strcmp(op,"-eq")) return atoi(a)==atoi(b)?0:1;
        if (!strcmp(op,"-ne")) return atoi(a)!=atoi(b)?0:1;
        if (!strcmp(op,"-lt")) return atoi(a)< atoi(b)?0:1;
        if (!strcmp(op,"-le")) return atoi(a)<=atoi(b)?0:1;
        if (!strcmp(op,"-gt")) return atoi(a)> atoi(b)?0:1;
        if (!strcmp(op,"-ge")) return atoi(a)>=atoi(b)?0:1;
    }
    if (n==2) return argv[1][0]!='\0'?0:1;
    return 1;
}

/* ── read ────────────────────────────────────────────────────────────────────── */

int builtin_read(int argc, char **argv, ShellContext *ctx) {
    int raw=0; const char *prompt=NULL, *varname="REPLY";
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"-r")) raw=1;
        else if (!strcmp(argv[i],"-p")&&i+1<argc) prompt=argv[++i];
        else varname=argv[i];
    }
    if (prompt) out(ctx,prompt);
    char buf[4096]={0}; (void)raw;
    ctx->io->read_line(ctx->io, buf, (int)sizeof(buf)-1);
    str_trim(buf); shell_setenv(ctx,varname,buf,false); return 0;
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

int builtin_setopt(int argc, char **argv, ShellContext *ctx) {
    for (int i=1; i<argc; i++) {
        const char *opt=argv[i]; int unset=0;
        if (str_startswith(opt,"NO_")||str_startswith(opt,"no_")) { unset=1; opt+=3; }
        int val=!unset;
        if      (!_stricmp(opt,"AUTO_CD"))           ctx->opts.auto_cd=val;
        else if (!_stricmp(opt,"CORRECT"))           ctx->opts.correct=val;
        else if (!_stricmp(opt,"GLOB_STAR")||
                 !_stricmp(opt,"GLOB_STAR_SHORT"))   ctx->opts.glob_star=val;
        else if (!_stricmp(opt,"HIST_IGNORE_DUPS"))  ctx->opts.hist_ignore_dups=val;
        else if (!_stricmp(opt,"SHARE_HISTORY"))     ctx->opts.share_history=val;
    }
    return 0;
}

/* ── jobs / fg / bg ──────────────────────────────────────────────────────────── */

int builtin_jobs(int argc, char **argv, ShellContext *ctx) {
    (void)argc;(void)argv; job_poll_all(&ctx->jobs); job_print_all(&ctx->jobs); return 0;
}
int builtin_fg(int argc, char **argv, ShellContext *ctx) {
    int id=0;
    if (argc>1) id=atoi(argv[1][0]=='%'?argv[1]+1:argv[1]);
    if (!id) for (int i=JOBS_MAX-1;i>=0;i--) if (ctx->jobs.jobs[i].id){id=ctx->jobs.jobs[i].id;break;}
    if (!id) { outln(ctx,"fg: no current job"); return 1; }
    return job_fg(&ctx->jobs,id);
}
int builtin_bg(int argc, char **argv, ShellContext *ctx) {
    int id=argc>1?atoi(argv[1][0]=='%'?argv[1]+1:argv[1]):1;
    return job_bg(&ctx->jobs,id)?0:1;
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
    /* command -v name  — print path if found, exit 1 if not (like which) */
    if (argc >= 3 && strcmp(argv[1], "-v") == 0) {
        char *p = shell_which(ctx, argv[2]);
        if (p) { outln(ctx, p); str_free(p); return 0; }
        return 1;
    }
    /* command name [args...]  — run name as external command */
    if (argc >= 2) {
        char *line = str_join(argv + 1, argc - 1, " ");
        int ret = shell_exec_line(ctx, line);
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

/* ── noop ────────────────────────────────────────────────────────────────────── */

int builtin_noop(int argc, char **argv, ShellContext *ctx) { (void)argc;(void)argv;(void)ctx; return 0; }