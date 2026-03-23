#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "builtins.h"
#include "shell.h"
#include "expand.h"
#include "util.h"

/* ─── Built-in dispatch table ────────────────────────────────────────────── */

static const BuiltinEntry BUILTINS[] = {
    { "cd",       builtin_cd       },
    { "echo",     builtin_echo     },
    { "printf",   builtin_printf_cmd },
    { "export",   builtin_export   },
    { "unset",    builtin_unset    },
    { "alias",    builtin_alias    },
    { "unalias",  builtin_unalias  },
    { "source",   builtin_source   },
    { ".",        builtin_source   },
    { "exit",     builtin_exit     },
    { "return",   builtin_return   },
    { "true",     builtin_true_cmd },
    { "false",    builtin_false_cmd},
    { "test",     builtin_test     },
    { "[",        builtin_test     },
    { "read",     builtin_read     },
    { "set",      builtin_set_cmd  },
    { "setopt",   builtin_setopt   },
    { "shopt",    builtin_setopt   },
    { "jobs",     builtin_jobs     },
    { "fg",       builtin_fg       },
    { "bg",       builtin_bg       },
    { "kill",     builtin_kill_cmd },
    { "wait",     builtin_wait     },
    { "pwd",      builtin_pwd      },
    { "type",     builtin_type     },
    { "which",    builtin_which    },
    { "eval",     builtin_eval     },
    { "exec",     builtin_exec     },
    { "local",    builtin_local    },
    { "typeset",  builtin_typeset  },
    { "declare",  builtin_typeset  },
    { "hash",     builtin_hash     },
    { "trap",     builtin_trap     },
    { "autoload", builtin_autoload },
    { "compdef",  builtin_compdef  },
    { "compctl",  builtin_compdef  },
    { "compinit", builtin_compinit },
    { "zstyle",   builtin_zstyle   },
    { "zle",      builtin_zle      },
    { "open",     builtin_open     },
    { "clip",     builtin_clip     },
    { "env",      builtin_env      },
    { "sudo",     builtin_sudo     },
    { "umask",    builtin_true_cmd }, /* no-op */
    { "ulimit",   builtin_true_cmd }, /* no-op */
    { NULL, NULL }
};

BuiltinFn builtin_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; BUILTINS[i].name; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return BUILTINS[i].fn;
    }
    return NULL;
}

/* ─── Helper: write text through shell's output callback ─────────────────── */

static void out(ShellContext *ctx, const char *s) {
    if (!s) return;
    if (ctx->write_output) ctx->write_output(s, (int)strlen(s), ctx->write_ud);
    else if (ctx->h_stdout != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(ctx->h_stdout, s, (DWORD)strlen(s), &w, NULL);
    }
}

static void outln(ShellContext *ctx, const char *s) {
    out(ctx, s); out(ctx, "\r\n");
}

/* ─── cd ─────────────────────────────────────────────────────────────────── */

int builtin_cd(int argc, char **argv, ShellContext *ctx) {
    const char *dir = NULL;
    if (argc < 2) {
        dir = shell_getenv(ctx, "HOME");
        if (!dir) dir = shell_getenv(ctx, "USERPROFILE");
        if (!dir) dir = "C:\\";
    } else if (strcmp(argv[1], "-") == 0) {
        dir = shell_getenv(ctx, "OLDPWD");
        if (!dir) { outln(ctx, "cd: OLDPWD not set"); return 1; }
        outln(ctx, dir);
    } else {
        dir = argv[1];
    }

    char *expanded = expand_tilde(ctx, dir);

    /* Save OLDPWD */
    shell_setenv(ctx, "OLDPWD", ctx->cwd, true);

    wchar_t *wdir = utf8_to_utf16(expanded, NULL);
    BOOL ok = wdir && SetCurrentDirectoryW(wdir);
    if (wdir) HeapFree(GetProcessHeap(), 0, wdir);
    HeapFree(GetProcessHeap(), 0, expanded);

    if (!ok) {
        char msg[512]; _snprintf(msg, sizeof(msg), "cd: %s: No such file or directory", dir);
        outln(ctx, msg);
        return 1;
    }

    /* Update $PWD */
    wchar_t wcwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, wcwd);
    char *ncwd = utf16_to_utf8(wcwd, NULL);
    if (ncwd) {
        strncpy(ctx->cwd, ncwd, MAX_PATH - 1);
        shell_setenv(ctx, "PWD", ncwd, true);
        HeapFree(GetProcessHeap(), 0, ncwd);
    }
    return 0;
}

/* ─── echo ───────────────────────────────────────────────────────────────── */

int builtin_echo(int argc, char **argv, ShellContext *ctx) {
    bool no_newline = false;
    bool interpret  = false;
    int  start      = 1;

    while (start < argc && argv[start][0] == '-') {
        bool valid = true;
        for (const char *p = argv[start] + 1; *p; p++) {
            if (*p == 'n') no_newline = true;
            else if (*p == 'e') interpret = true;
            else if (*p == 'E') interpret = false;
            else { valid = false; break; }
        }
        if (!valid) break;
        start++;
    }

    for (int i = start; i < argc; i++) {
        if (i > start) out(ctx, " ");
        if (interpret) {
            /* Process escape sequences */
            for (const char *p = argv[i]; *p; p++) {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                        case 'n': out(ctx, "\n"); break;
                        case 'r': out(ctx, "\r"); break;
                        case 't': out(ctx, "\t"); break;
                        case '\\': out(ctx, "\\"); break;
                        case 'a': out(ctx, "\a"); break;
                        case 'b': out(ctx, "\b"); break;
                        case 'e': out(ctx, "\x1B"); break;
                        case '0': {
                            unsigned char c = 0;
                            for (int j = 0; j < 3 && isdigit((unsigned char)p[1]); j++)
                                c = c * 8 + (*++p - '0');
                            char cs[2] = { (char)c, 0 }; out(ctx, cs); break;
                        }
                        default: { char cs[3] = { '\\', *p, 0 }; out(ctx, cs); break; }
                    }
                } else {
                    char cs[2] = { *p, 0 }; out(ctx, cs);
                }
            }
        } else {
            out(ctx, argv[i]);
        }
    }
    if (!no_newline) out(ctx, "\r\n");
    return 0;
}

/* ─── printf ─────────────────────────────────────────────────────────────── */

int builtin_printf_cmd(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { outln(ctx, "printf: missing format"); return 1; }
    char buf[4096];
    const char *fmt = argv[1];
    /* For MVP: snprintf with up to 4 args */
    switch (argc - 2) {
        case 0: _snprintf(buf, sizeof(buf), fmt); break;
        case 1: _snprintf(buf, sizeof(buf), fmt, argv[2]); break;
        case 2: _snprintf(buf, sizeof(buf), fmt, argv[2], argv[3]); break;
        case 3: _snprintf(buf, sizeof(buf), fmt, argv[2], argv[3], argv[4]); break;
        default: _snprintf(buf, sizeof(buf), fmt, argv[2], argv[3], argv[4], argv[5]); break;
    }
    out(ctx, buf);
    return 0;
}

/* ─── export ─────────────────────────────────────────────────────────────── */

int builtin_export(int argc, char **argv, ShellContext *ctx) {
    if (argc == 1) {
        /* Print all exported vars */
        EnvScope *scope = ctx->env;
        while (scope) {
            for (EnvVar *v = scope->vars; v; v = v->next) {
                if (v->exported) {
                    char line[1024];
                    _snprintf(line, sizeof(line), "export %s=\"%s\"", v->name, v->value ? v->value : "");
                    outln(ctx, line);
                }
            }
            scope = scope->parent;
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char name[256] = {0};
            strncpy(name, argv[i], (size_t)(eq - argv[i]));
            shell_setenv(ctx, name, eq + 1, true);
        } else {
            /* Mark existing variable as exported */
            shell_setenv(ctx, argv[i], shell_getenv(ctx, argv[i]), true);
        }
    }
    return 0;
}

/* ─── unset ──────────────────────────────────────────────────────────────── */

int builtin_unset(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) shell_unsetenv(ctx, argv[i]);
    return 0;
}

/* ─── alias / unalias ────────────────────────────────────────────────────── */

int builtin_alias(int argc, char **argv, ShellContext *ctx) {
    if (argc == 1) {
        for (Alias *a = ctx->aliases; a; a = a->next) {
            char line[512]; _snprintf(line, sizeof(line), "alias %s='%s'", a->name, a->value);
            outln(ctx, line);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) {
            /* Print alias */
            for (Alias *a = ctx->aliases; a; a = a->next) {
                if (strcmp(a->name, argv[i]) == 0) {
                    char line[512]; _snprintf(line, sizeof(line), "alias %s='%s'", a->name, a->value);
                    outln(ctx, line); break;
                }
            }
            continue;
        }
        char name[256] = {0};
        strncpy(name, argv[i], (size_t)(eq - argv[i]));
        const char *val = eq + 1;
        /* Remove surrounding quotes */
        if ((val[0] == '\'' || val[0] == '"') && val[strlen(val)-1] == val[0]) {
            char *v = str_ndup(val + 1, strlen(val) - 2);
            /* Find or create alias */
            for (Alias *a = ctx->aliases; a; a = a->next) {
                if (strcmp(a->name, name) == 0) {
                    HeapFree(GetProcessHeap(), 0, a->value);
                    a->value = v; goto next_alias;
                }
            }
            Alias *na = (Alias *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Alias));
            na->name  = str_dup(name);
            na->value = v;
            na->next  = ctx->aliases;
            ctx->aliases = na;
        } else {
            Alias *na = (Alias *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Alias));
            na->name  = str_dup(name);
            na->value = str_dup(val);
            na->next  = ctx->aliases;
            ctx->aliases = na;
        }
        next_alias:;
    }
    return 0;
}

int builtin_unalias(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) {
        Alias **pp = &ctx->aliases;
        while (*pp) {
            if (strcmp((*pp)->name, argv[i]) == 0) {
                Alias *dead = *pp;
                *pp = dead->next;
                HeapFree(GetProcessHeap(), 0, dead->name);
                HeapFree(GetProcessHeap(), 0, dead->value);
                HeapFree(GetProcessHeap(), 0, dead);
                break;
            }
            pp = &(*pp)->next;
        }
    }
    return 0;
}

/* ─── source ─────────────────────────────────────────────────────────────── */

int builtin_source(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { outln(ctx, "source: filename required"); return 1; }
    char *path = expand_tilde(ctx, argv[1]);
    int ret = shell_source(ctx, path);
    HeapFree(GetProcessHeap(), 0, path);
    return ret;
}

/* ─── exit / return ──────────────────────────────────────────────────────── */

int builtin_exit(int argc, char **argv, ShellContext *ctx) {
    int code = argc > 1 ? atoi(argv[1]) : ctx->last_status;
    ctx->exit_requested = true;
    ctx->exit_code      = code;
    return code;
}

int builtin_return(int argc, char **argv, ShellContext *ctx) {
    int code = argc > 1 ? atoi(argv[1]) : ctx->last_status;
    ctx->last_status = code;
    return code;
}

/* ─── true / false ───────────────────────────────────────────────────────── */

int builtin_true_cmd(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 0;
}

int builtin_false_cmd(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 1;
}

/* ─── test / [ ───────────────────────────────────────────────────────────── */

int builtin_test(int argc, char **argv, ShellContext *ctx) {
    /* Remove trailing ] if invoked as [ */
    int n = argc;
    if (n > 1 && strcmp(argv[n-1], "]") == 0) n--;

    if (n < 2) return 1;

    /* Unary ops */
    if (n == 3 && argv[1][0] == '-' && argv[1][2] == '\0') {
        const char *file = argv[2];
        char op = argv[1][1];
        switch (op) {
            case 'z': return strlen(file) == 0 ? 0 : 1;
            case 'n': return strlen(file) != 0 ? 0 : 1;
            case 'f': return path_exists(file) && !path_is_dir(file) ? 0 : 1;
            case 'd': return path_is_dir(file) ? 0 : 1;
            case 'e': return path_exists(file) ? 0 : 1;
            case 'r': return path_exists(file) ? 0 : 1; /* simplified */
            case 'w': return path_exists(file) ? 0 : 1;
            case 'x': return path_exists(file) ? 0 : 1;
            case 's': {
                wchar_t *w = utf8_to_utf16(file, NULL);
                WIN32_FILE_ATTRIBUTE_DATA fa;
                bool ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, &fa);
                if (w) HeapFree(GetProcessHeap(), 0, w);
                return (ok && (fa.nFileSizeLow > 0 || fa.nFileSizeHigh > 0)) ? 0 : 1;
            }
            default: return 1;
        }
    }

    /* Binary ops */
    if (n == 4) {
        const char *a = argv[1], *op = argv[2], *b = argv[3];
        if (!strcmp(op, "=")  || !strcmp(op, "==")) return strcmp(a, b) == 0 ? 0 : 1;
        if (!strcmp(op, "!="))                       return strcmp(a, b) != 0 ? 0 : 1;
        if (!strcmp(op, "<"))                        return strcmp(a, b) <  0 ? 0 : 1;
        if (!strcmp(op, ">"))                        return strcmp(a, b) >  0 ? 0 : 1;
        if (!strcmp(op, "-eq")) return atoi(a) == atoi(b) ? 0 : 1;
        if (!strcmp(op, "-ne")) return atoi(a) != atoi(b) ? 0 : 1;
        if (!strcmp(op, "-lt")) return atoi(a) <  atoi(b) ? 0 : 1;
        if (!strcmp(op, "-le")) return atoi(a) <= atoi(b) ? 0 : 1;
        if (!strcmp(op, "-gt")) return atoi(a) >  atoi(b) ? 0 : 1;
        if (!strcmp(op, "-ge")) return atoi(a) >= atoi(b) ? 0 : 1;
    }

    /* Single string: true if non-empty */
    if (n == 2) return argv[1][0] != '\0' ? 0 : 1;
    return 1;
    (void)ctx;
}

/* ─── read ───────────────────────────────────────────────────────────────── */

int builtin_read(int argc, char **argv, ShellContext *ctx) {
    bool raw = false;
    const char *prompt = NULL;
    const char *varname = "REPLY";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) { raw = true; }
        else if (!strcmp(argv[i], "-p") && i+1 < argc) { prompt = argv[++i]; }
        else { varname = argv[i]; }
    }
    if (prompt) out(ctx, prompt);
    char buf[4096] = {0};
    DWORD n = 0;
    ReadFile(ctx->h_stdin, buf, sizeof(buf)-1, &n, NULL);
    buf[n] = '\0';
    str_trim(buf);
    (void)raw;
    shell_setenv(ctx, varname, buf, false);
    return 0;
}

/* ─── set ────────────────────────────────────────────────────────────────── */

int builtin_set_cmd(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char *p = argv[i]+1; *p; p++) {
                switch (*p) {
                    case 'e': ctx->opts.err_exit  = true;  break;
                    case 'x': ctx->opts.xtrace    = true;  break;
                    case 'u': ctx->opts.nounset   = true;  break;
                    case 'E': ctx->opts.err_exit  = false; break;
                    case 'X': ctx->opts.xtrace    = false; break;
                    default: break;
                }
            }
        }
    }
    return 0;
}

/* ─── setopt / shopt ─────────────────────────────────────────────────────── */

int builtin_setopt(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) {
        const char *opt = argv[i];
        /* Handle NO_ prefix (unset option) */
        bool unset = false;
        if (str_startswith(opt, "NO_") || str_startswith(opt, "no_")) {
            unset = true; opt += 3;
        }
        bool val = !unset;
        if (!_stricmp(opt, "AUTO_CD"))           ctx->opts.auto_cd          = val;
        else if (!_stricmp(opt, "CORRECT"))      ctx->opts.correct           = val;
        else if (!_stricmp(opt, "GLOB_STAR_SHORT")) ctx->opts.glob_star_short = val;
        else if (!_stricmp(opt, "HIST_IGNORE_DUPS")) { ctx->opts.hist_ignore_dups = val; ctx->history.ignore_dups = val; }
        else if (!_stricmp(opt, "SHARE_HISTORY")) ctx->opts.share_history    = val;
    }
    return 0;
}

/* ─── jobs / fg / bg ─────────────────────────────────────────────────────── */

int builtin_jobs(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv;
    job_poll_all(&ctx->jobs);
    job_print_all(&ctx->jobs);
    return 0;
}

int builtin_fg(int argc, char **argv, ShellContext *ctx) {
    int id = (argc > 1) ? atoi(argv[1] + (argv[1][0] == '%' ? 1 : 0)) : ctx->jobs.fg_job_id;
    if (!id) {
        /* Use most recent job */
        for (int i = JOBS_MAX - 1; i >= 0; i--) {
            if (ctx->jobs.jobs[i].id) { id = ctx->jobs.jobs[i].id; break; }
        }
    }
    if (!id) { outln(ctx, "fg: no current job"); return 1; }
    return job_fg(&ctx->jobs, id);
}

int builtin_bg(int argc, char **argv, ShellContext *ctx) {
    int id = (argc > 1) ? atoi(argv[1] + (argv[1][0] == '%' ? 1 : 0)) : 1;
    return job_bg(&ctx->jobs, id) ? 0 : 1;
}

/* ─── kill ───────────────────────────────────────────────────────────────── */

int builtin_kill_cmd(int argc, char **argv, ShellContext *ctx) {
    int sig = 15, start = 1;
    if (argc > 1 && argv[1][0] == '-') {
        sig = atoi(argv[1] + 1); start = 2;
    }
    for (int i = start; i < argc; i++) {
        if (argv[i][0] == '%') {
            job_kill(&ctx->jobs, atoi(argv[i]+1), sig);
        } else {
            DWORD pid = (DWORD)atoi(argv[i]);
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) { TerminateProcess(h, (UINT)sig); CloseHandle(h); }
        }
    }
    return 0;
}

/* ─── wait ───────────────────────────────────────────────────────────────── */

int builtin_wait(int argc, char **argv, ShellContext *ctx) {
    if (argc > 1) {
        int id = atoi(argv[1] + (argv[1][0] == '%' ? 1 : 0));
        Job *j = job_find(&ctx->jobs, id);
        if (j && j->hprocess) WaitForSingleObject(j->hprocess, INFINITE);
    } else {
        /* Wait for all jobs */
        for (int i = 0; i < JOBS_MAX; i++) {
            Job *j = &ctx->jobs.jobs[i];
            if (j->id && j->hprocess) WaitForSingleObject(j->hprocess, INFINITE);
        }
    }
    job_poll_all(&ctx->jobs);
    return 0;
}

/* ─── pwd ────────────────────────────────────────────────────────────────── */

int builtin_pwd(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv;
    outln(ctx, ctx->cwd);
    return 0;
}

/* ─── type ───────────────────────────────────────────────────────────────── */

int builtin_type(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) {
        char line[512];
        if (builtin_find(argv[i])) {
            _snprintf(line, sizeof(line), "%s is a shell builtin", argv[i]);
        } else {
            /* Check aliases */
            bool found_alias = false;
            for (Alias *a = ctx->aliases; a; a = a->next) {
                if (strcmp(a->name, argv[i]) == 0) {
                    _snprintf(line, sizeof(line), "%s is an alias for '%s'", argv[i], a->value);
                    found_alias = true; break;
                }
            }
            if (!found_alias) {
                char *path = shell_which(ctx, argv[i]);
                if (path) {
                    _snprintf(line, sizeof(line), "%s is %s", argv[i], path);
                    HeapFree(GetProcessHeap(), 0, path);
                } else {
                    _snprintf(line, sizeof(line), "%s not found", argv[i]);
                }
            }
        }
        outln(ctx, line);
    }
    return 0;
}

/* ─── which ──────────────────────────────────────────────────────────────── */

int builtin_which(int argc, char **argv, ShellContext *ctx) {
    for (int i = 1; i < argc; i++) {
        char *p = shell_which(ctx, argv[i]);
        if (p) { outln(ctx, p); HeapFree(GetProcessHeap(), 0, p); }
        else {
            char msg[256]; _snprintf(msg, sizeof(msg), "%s not found", argv[i]); outln(ctx, msg);
        }
    }
    return 0;
}

/* ─── eval ───────────────────────────────────────────────────────────────── */

int builtin_eval(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) return 0;
    /* Join all args with spaces */
    char *line = str_join(argv + 1, argc - 1, " ");
    int ret = shell_exec_line(ctx, line);
    HeapFree(GetProcessHeap(), 0, line);
    return ret;
}

/* ─── exec ───────────────────────────────────────────────────────────────── */

int builtin_exec(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) return 0;
    /* Replace shell with command — set exit_requested */
    ctx->exit_requested = true;
    char *line = str_join(argv + 1, argc - 1, " ");
    int ret = shell_exec_line(ctx, line);
    HeapFree(GetProcessHeap(), 0, line);
    ctx->exit_code = ret;
    return ret;
}

/* ─── local ──────────────────────────────────────────────────────────────── */

int builtin_local(int argc, char **argv, ShellContext *ctx) {
    /* Declare variables as local (in current scope) */
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char name[256] = {0};
            strncpy(name, argv[i], (size_t)(eq - argv[i]));
            shell_setenv(ctx, name, eq + 1, false);
        } else {
            shell_setenv(ctx, argv[i], "", false);
        }
    }
    return 0;
}

/* ─── typeset / declare ──────────────────────────────────────────────────── */

int builtin_typeset(int argc, char **argv, ShellContext *ctx) {
    /* Simplified: treat like export for -x, else like local */
    bool do_export = false, do_integer = false;
    int start = 1;
    for (; start < argc && argv[start][0] == '-'; start++) {
        for (const char *p = argv[start]+1; *p; p++) {
            if (*p == 'x') do_export = true;
            if (*p == 'i') do_integer = true;
        }
    }
    for (int i = start; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char name[256] = {0};
            strncpy(name, argv[i], (size_t)(eq - argv[i]));
            shell_setenv(ctx, name, eq + 1, do_export);
            if (do_integer) {
                /* Evaluate as arithmetic */
                long v = expand_arith(ctx, eq + 1);
                char buf[32]; _snprintf(buf, sizeof(buf), "%ld", v);
                shell_setenv(ctx, name, buf, do_export);
            }
        } else {
            shell_setenv(ctx, argv[i], shell_getenv(ctx, argv[i]), do_export);
        }
    }
    return 0;
}

/* ─── hash ───────────────────────────────────────────────────────────────── */

int builtin_hash(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx;
    /* Command hash table — no-op for now; PATH is searched each time */
    return 0;
}

/* ─── trap ───────────────────────────────────────────────────────────────── */

int builtin_trap(int argc, char **argv, ShellContext *ctx) {
    /* Simplified: store handler strings for signals */
    (void)argc; (void)argv; (void)ctx;
    return 0;
}

/* ─── autoload ───────────────────────────────────────────────────────────── */

int builtin_autoload(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx;
    return 0;
}

/* ─── compdef / compctl / compinit / zstyle / zle — stubs ───────────────── */

int builtin_compdef(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 0;
}
int builtin_compinit(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 0;
}
int builtin_zstyle(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 0;
}
int builtin_zle(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv; (void)ctx; return 0;
}

/* ─── open ───────────────────────────────────────────────────────────────── */

int builtin_open(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { outln(ctx, "open: filename required"); return 1; }
    wchar_t *wf = utf8_to_utf16(argv[1], NULL);
    if (!wf) return 1;
    HINSTANCE hi = ShellExecuteW(NULL, L"open", wf, NULL, NULL, SW_SHOWNORMAL);
    HeapFree(GetProcessHeap(), 0, wf);
    return ((INT_PTR)hi > 32) ? 0 : 1;
}

/* ─── clip ───────────────────────────────────────────────────────────────── */

int builtin_clip(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv;
    /* Read all stdin, copy to clipboard */
    char buf[65536] = {0}; DWORD n = 0;
    ReadFile(ctx->h_stdin, buf, sizeof(buf)-1, &n, NULL);
    buf[n] = '\0';
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n + 1);
        if (hg) {
            char *p = (char *)GlobalLock(hg);
            if (p) { memcpy(p, buf, n + 1); GlobalUnlock(hg); }
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
    return 0;
}

/* ─── env ────────────────────────────────────────────────────────────────── */

int builtin_env(int argc, char **argv, ShellContext *ctx) {
    (void)argc; (void)argv;
    /* Print all environment variables */
    wchar_t *env = GetEnvironmentStringsW();
    if (!env) return 1;
    for (wchar_t *p = env; *p; p += wcslen(p) + 1) {
        char *line = utf16_to_utf8(p, NULL);
        if (line) { outln(ctx, line); HeapFree(GetProcessHeap(), 0, line); }
    }
    FreeEnvironmentStringsW(env);
    return 0;
}

/* ─── sudo ───────────────────────────────────────────────────────────────── */

int builtin_sudo(int argc, char **argv, ShellContext *ctx) {
    if (argc < 2) { outln(ctx, "sudo: command required"); return 1; }
    char *cmdline = str_join(argv + 1, argc - 1, " ");
    wchar_t *wcmd = utf8_to_utf16(cmdline, NULL);
    HeapFree(GetProcessHeap(), 0, cmdline);
    if (!wcmd) return 1;
    HINSTANCE hi = ShellExecuteW(NULL, L"runas", wcmd, NULL, NULL, SW_SHOWNORMAL);
    HeapFree(GetProcessHeap(), 0, wcmd);
    return ((INT_PTR)hi > 32) ? 0 : 1;
}
