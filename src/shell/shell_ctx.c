/*
 * shell_ctx.c — ShellContext lifecycle and high-level execution API.
 *
 * This is the "façade" that glues together the lexer, parser, and executor.
 * It also implements shell_which, shell_source, shell_expand_prompt, and the
 * env wrapper functions that forward to env.h.
 */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "shell_ctx.h"
#include "env.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "expand.h"
#include "history.h"
#include "jobs.h"
#include "builtins.h"
#include "../core/str_util.h"
#include "../core/path_util.h"
#include "../core/log.h"
#include "../core/unicode.h"
#include "../ai/wsh_ai.h"

/* ── Handle fields (child process I/O; stored directly in ctx) ────────────── */
/*
 * We extend ShellContext at runtime with Win32 HANDLE fields for child I/O.
 * These are NOT exposed in the header to keep it platform-agnostic.
 * Instead, executor.c accesses them via the accessor macros below.
 * This demonstrates encapsulation: the HANDLE members are hidden from the
 * rest of the codebase.
 */
HANDLE ctx_stdin(ShellContext *ctx)  { return ctx->h_stdin; }
HANDLE ctx_stdout(ShellContext *ctx) { return ctx->h_stdout; }
HANDLE ctx_stderr(ShellContext *ctx) { return ctx->h_stderr; }

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

void shell_ctx_init(ShellContext *ctx, IShellIO *io) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->io        = io;
    ctx->env       = env_scope_push(NULL);
    ctx->arena     = arena_create(65536);
    ctx->shell_pid = GetCurrentProcessId();
    ctx->cancel_requested = 0;

    /* Default I/O handles — replaced if PTY is active */
    ctx->h_stdin  = GetStdHandle(STD_INPUT_HANDLE);
    ctx->h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    ctx->h_stderr = GetStdHandle(STD_ERROR_HANDLE);

    /* Import process environment so $PATH, $HOME etc. are visible */
    env_import_process(ctx->env);

    /* Make companion utilities from the dist/executable directory available
     * without requiring the user to edit PATH manually. */
    {
        char exe_dir[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
        char *bs = strrchr(exe_dir, '\\');
        if (bs) {
            *bs = '\0';
            const char *old_path = env_get(ctx->env, "PATH");
            if (old_path && !strstr(old_path, exe_dir)) {
                char merged[32768];
                _snprintf(merged, sizeof(merged), "%s;%s", exe_dir, old_path);
                env_set(ctx->env, "PATH", merged, true);
                SetEnvironmentVariableA("PATH", merged);
            }
        }
    }

    /* Ensure HOME and PWD are set */
    if (!env_get(ctx->env, "HOME")) {
        char profile[MAX_PATH] = {0};
        GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
        if (profile[0]) env_set(ctx->env, "HOME", profile, true);
    }

    /* Sync CWD */
    wchar_t wcwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, wcwd);
    char *cwd = u16_to_u8(wcwd, NULL);
    if (cwd) { strncpy(ctx->cwd, cwd, MAX_PATH-1); str_free(cwd); }
    env_set(ctx->env, "PWD",   ctx->cwd, true);
    env_set(ctx->env, "SHELL", "wsh",    true);
    env_set(ctx->env, "TERM",  "xterm-256color", true);
    env_set(ctx->env, "COLORTERM", "truecolor", true);
    env_set(ctx->env, "WSH_TERM", "1", true);
    env_set(ctx->env, "IFS",   " \t\n",  false);

    /* AI assistant: enabled by default */
    ctx->ai_enabled = true;
    ctx->ai_state = wsh_ai_state_create();
    ctx->last_command[0] = '\0';
    ctx->last_stderr_snippet[0] = '\0';

    /* Shell options: interactive mode on by default */
    ctx->opts.interactive = 1;

    history_init(&ctx->history);
    job_table_init(&ctx->jobs);
    scheduler_init(&ctx->scheduler);

    WSH_LOG_DEBUG("shell_ctx_init complete, pid=%lu", (unsigned long)ctx->shell_pid);
}

void shell_ctx_free(ShellContext *ctx) {
    history_save(&ctx->history);
    history_free(&ctx->history);
    job_table_free(&ctx->jobs);
    scheduler_free(&ctx->scheduler);

    /* Free aliases */
    for (Alias *a = ctx->aliases; a;) {
        Alias *next = a->next;
        str_free(a->name); str_free(a->value);
        HeapFree(GetProcessHeap(), 0, a);
        a = next;
    }

    /* Free functions (bodies are arena-owned; only free names) */
    for (ShellFunc *f = ctx->functions; f;) {
        ShellFunc *next = f->next;
        str_free(f->name);
        HeapFree(GetProcessHeap(), 0, f);
        f = next;
    }

    /* Free trap handlers */
    for (int i = 0; i < TRAP_COUNT; i++) str_free(ctx->traps[i]);

    /* Free AI state */
    if (ctx->ai_state) {
        wsh_ai_state_free(ctx->ai_state);
        ctx->ai_state = NULL;
    }

    /* Free env scope chain */
    while (ctx->env) ctx->env = env_scope_pop(ctx->env);

    if (ctx->arena) arena_destroy(ctx->arena);
}

/* ── Execution ─────────────────────────────────────────────────────────────── */

int shell_exec_line(ShellContext *ctx, const char *line) {
    if (!line || !line[0]) return 0;

    /* zsh has several internal execution contexts (aliases, eval, command
     * substitution, prompt hooks). Wsh used to reuse one arena and reset it on
     * every shell_exec_line() call. That invalidated the currently executing
     * AST when shell_exec_line() was called from inside expansion/alias logic.
     * Only the outermost call is allowed to reset the arena. */
    if (ctx->exec_depth > 64 || ctx->call_depth > 64) {
        io_writeln(ctx->io, "wsh: maximum nesting depth exceeded");
        return 1;
    }

    bool top_level_exec = (ctx->exec_depth == 0);
    ctx->exec_depth++;

    int ret = 0;

    /* History expansion: !! !n !str */
    char expanded_line[8192];
    if (line[0] == '!') {
        if (!history_expand(&ctx->history, line, expanded_line, sizeof(expanded_line)))
            strncpy(expanded_line, line, sizeof(expanded_line)-1);
        expanded_line[sizeof(expanded_line)-1] = '\0';
        line = expanded_line;
    }

    /* AUTO_CD: if the word is a directory, cd into it */
    if (ctx->opts.auto_cd) {
        char *trial = expand_tilde(ctx, line);
        if (path_is_dir(trial)) {
            char *argv2[2] = { "cd", trial };
            ret = builtin_cd(2, argv2, ctx);
            str_free(trial);
            ctx->exec_depth--;
            return ret;
        }
        str_free(trial);
    }

    /* Tokenise → Parse → Execute */
    if (top_level_exec && !ctx->preserve_ast_arena) arena_reset(ctx->arena);

    Lexer  lex;  lex_init(&lex, line, ctx->arena);
    Parser parser; parser_init(&parser, &lex, ctx->arena);
    ASTNode *ast = parser_parse(&parser);

    if (parser.error) {
        io_writeln(ctx->io, parser.errmsg);
        ret = 2; /* syntax error */
    } else if (!ast) {
        ret = 0;
    } else {
        ret = exec_node(ctx, ast);
        ctx->last_status = ret;
    }

    /* Store last command at top-level execution only */
    if (top_level_exec) {
        strncpy(ctx->last_command, line, sizeof(ctx->last_command) - 1);
        ctx->last_command[sizeof(ctx->last_command) - 1] = '\0';
    }

    ctx->exec_depth--;
    return ret;
}

int shell_source(ShellContext *ctx, const char *path) {
    if (!path) return 1;
    wchar_t *wp = u8_to_u16(path, NULL);
    FILE *f = wp ? _wfopen(wp, L"rb") : NULL;
    str_free(wp);

    if (!f) {
        char msg[512]; _snprintf(msg, sizeof(msg), "wsh: source: %s: not found", path);
        io_writeln(ctx->io, msg);
        return 1;
    }

    /* Read entire file into memory so multi-line constructs (functions, if, for)
     * are parsed as complete units rather than broken at line boundaries. */
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz <= 0) { fclose(f); return 0; }

    char *raw = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)fsz + 1);
    if (!raw) { fclose(f); return 1; }
    size_t nr = fread(raw, 1, (size_t)fsz, f);
    raw[nr] = '\0';
    fclose(f);

    char *buf = raw;
    if (nr >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        buf = str_ndup(raw + 3, nr - 3);
        str_free(raw);
    } else if (!wsh_utf8_validate_n(raw, (int)nr)) {
        int out_len = 0;
        buf = wsh_bytes_to_utf8_for_terminal(raw, (int)nr, &out_len);
        str_free(raw);
    }
    if (!buf) return 1;

    ctx->call_depth++;
    int ret = 0;

    /* Parse and execute all statements in the file.
     * Do not reset the arena while source is executed as part of a larger
     * already parsed list: the right side of `source file; next` still lives
     * in that arena. */
    if (ctx->exec_depth == 0 && !ctx->preserve_ast_arena) arena_reset(ctx->arena);
    Lexer lex; lex_init(&lex, buf, ctx->arena);
    Parser parser; parser_init(&parser, &lex, ctx->arena);

    while (!ctx->exit_requested) {
        /* Skip bare newlines/semis between statements using parser's own advance */
        while (parser.cur.kind == TOK_NEWLINE || parser.cur.kind == TOK_SEMI) {
            parser.cur = lex_next(parser.lex);
        }
        if (parser.cur.kind == TOK_EOF) break;

        parser.error = 0; parser.errmsg[0] = '\0';
        ASTNode *ast = parser_parse(&parser);

        if (parser.error) {
            io_writeln(ctx->io, parser.errmsg);
            ret = 2;
            break;
        }
        if (!ast) break;

        ret = exec_node(ctx, ast);
        ctx->last_status = ret;
    }

    ctx->call_depth--;
    HeapFree(GetProcessHeap(), 0, buf);
    return ret;
}

/* ── Environment wrappers ─────────────────────────────────────────────────── */

const char *shell_getenv(const ShellContext *ctx, const char *name) {
    return env_get(ctx->env, name);
}

void shell_setenv(ShellContext *ctx, const char *name, const char *value, bool exported) {
    env_set(ctx->env, name, value, exported);
}

void shell_unsetenv(ShellContext *ctx, const char *name) {
    env_unset(ctx->env, name);
}

/* ── Which ─────────────────────────────────────────────────────────────────── */

char *shell_which(ShellContext *ctx, const char *name) {
    if (!name) return NULL;
    static const char *EXT[] = { "", ".exe", ".cmd", ".bat", NULL };

    if (strchr(name, '\\') || strchr(name, '/')) {
        for (int e = 0; EXT[e]; e++) {
            char full[MAX_PATH]; _snprintf(full, MAX_PATH, "%s%s", name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) return str_dup(full);
        }
        return NULL;
    }

    char pathenv[32768] = {0};
    GetEnvironmentVariableA("PATH", pathenv, sizeof(pathenv));
    char **dirs = NULL; int nd = str_split(pathenv, ';', &dirs);
    char *result = NULL;
    for (int d = 0; d < nd && !result; d++) {
        if (!dirs[d] || !dirs[d][0]) continue;
        for (int e = 0; EXT[e] && !result; e++) {
            char full[MAX_PATH]; _snprintf(full, MAX_PATH, "%s\\%s%s", dirs[d], name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) result = str_dup(full);
        }
    }
    str_split_free(dirs, nd);
    (void)ctx;
    return result;
}

/* ── Prompt expansion ─────────────────────────────────────────────────────── */


static void prompt_push(char *buf, int *bi, int cap, const char *s) {
    if (!s) return;
    while (*s && *bi < cap - 1) buf[(*bi)++] = *s++;
}

static const char *path_basename_u8(const char *path) {
    if (!path || !*path) return "";
    const char *end = path + strlen(path);
    while (end > path && (end[-1] == '\\' || end[-1] == '/')) end--;
    const char *p = end;
    while (p > path && p[-1] != '\\' && p[-1] != '/') p--;
    return p;
}


char *shell_expand_prompt(ShellContext *ctx, const char *fmt) {
    if (!fmt) fmt = "%~ %# ";
    char buf[1024]; int bi = 0;
    for (const char *p = fmt; *p && bi < 1020; p++) {
        if (*p == '%' && p[1]) {
            p++;
            switch (*p) {
                case '~': { /* abbreviated CWD */
                    const char *home = shell_getenv(ctx, "HOME");
                    const char *cwd  = ctx->cwd;
                    if (home && str_startswith(cwd, home)) {
                        buf[bi++] = '~';
                        const char *rest = cwd + strlen(home);
                        while (*rest && bi < 1020) buf[bi++] = *rest++;
                    } else {
                        while (*cwd && bi < 1020) buf[bi++] = *cwd++;
                    }
                    break;
                }
                case '/':
                case 'd': { /* full CWD */
                    prompt_push(buf, &bi, sizeof(buf), ctx->cwd);
                    break;
                }
                case 'c':
                case 'C': { /* basename of CWD */
                    prompt_push(buf, &bi, sizeof(buf), path_basename_u8(ctx->cwd));
                    break;
                }
                case 'n': { /* username */
                    char user[128] = {0}; DWORD ul = sizeof(user);
                    GetUserNameA(user, &ul);
                    for (char *u = user; *u && bi < 1020; u++) buf[bi++] = *u;
                    break;
                }
                case 'm': { /* hostname */
                    char host[128] = {0}; DWORD hl = sizeof(host);
                    GetComputerNameA(host, &hl);
                    char *dot = strchr(host, '.'); if (dot) *dot = '\0';
                    for (char *h = host; *h && bi < 1020; h++) buf[bi++] = *h;
                    break;
                }
                case '#':
                case '$': { /* # for root, $ for normal */
                    /* On Windows check elevation */
                    HANDLE tok = NULL; BOOL elevated = FALSE;
                    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
                        TOKEN_ELEVATION te = {0}; DWORD tl = sizeof(te);
                        GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &tl);
                        elevated = te.TokenIsElevated;
                        CloseHandle(tok);
                    }
                    buf[bi++] = elevated ? '#' : '$';
                    break;
                }
                case '?': { /* last status */
                    char s[8]; _snprintf(s, sizeof(s), "%d", ctx->last_status);
                    for (char *x = s; *x && bi < 1020; x++) buf[bi++] = *x;
                    break;
                }
                case 'j': { /* job count */
                    char s[8]; _snprintf(s, sizeof(s), "%d", ctx->jobs.count);
                    for (char *x = s; *x && bi < 1020; x++) buf[bi++] = *x;
                    break;
                }
                case 'D': { /* date (simplified: YYYY-MM-DD) */
                    SYSTEMTIME st; GetLocalTime(&st);
                    char s[16]; _snprintf(s, sizeof(s), "%04d-%02d-%02d",
                                          st.wYear, st.wMonth, st.wDay);
                    for (char *x = s; *x && bi < 1020; x++) buf[bi++] = *x;
                    break;
                }
                case 't':
                case 'T':
                case '*': { /* time HH:MM:SS */
                    SYSTEMTIME st; GetLocalTime(&st);
                    char s[16]; _snprintf(s, sizeof(s), "%02d:%02d:%02d",
                                          st.wHour, st.wMinute, st.wSecond);
                    prompt_push(buf, &bi, sizeof(buf), s);
                    break;
                }
                case 'w': { /* date, short zsh-like */
                    SYSTEMTIME st; GetLocalTime(&st);
                    char s[16]; _snprintf(s, sizeof(s), "%02d/%02d/%02d",
                                          st.wMonth, st.wDay, st.wYear % 100);
                    prompt_push(buf, &bi, sizeof(buf), s);
                    break;
                }
                case '%': buf[bi++] = '%'; break;
                case 'N': buf[bi++] = '\n'; break;
                default:  buf[bi++] = '%'; buf[bi++] = *p; break;
            }
        } else {
            buf[bi++] = *p;
        }
    }
    buf[bi] = '\0';
    return str_dup(buf);
}

/* ── Scheduler tick ────────────────────────────────────────────────────────── */

int shell_scheduler_tick(ShellContext *ctx) {
    return scheduler_tick(&ctx->scheduler, ctx);
}
