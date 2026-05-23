/*
 * executor.c — Visits and executes each ASTNode kind.
 *
 * External command execution always uses CreateProcessW so the child inherits
 * the correctly redirected handles.  Pipes are Win32 anonymous pipes so that
 * both sides can be passed as inheritable handles to CreateProcessW.
 *
 * For foreground processes we WaitForSingleObject then read the exit code.
 * Background processes are registered in the JobTable; the UI thread polls
 * them via WM_TIMER.
 *
 * Redirection strategy:
 *   exec_apply_redirs() saves the three main handles (h_stdin/out/err) in
 *   the ShellContext and replaces them with file/pipe handles.
 *   exec_restore_redirs() closes any temporary handles and restores the saved
 *   ones.  This is safe because the executor never runs commands concurrently
 *   on the same ShellContext.
 */
#include <windows.h>
#include <ctype.h>
#include <shlwapi.h>
#include "parser.h"
#include <string.h>
#include <stdio.h>
#include "executor.h"
#include "builtins.h"
#include "expand.h"
#include "env.h"
#include "jobs.h"
#include "../core/str_util.h"
#include "../core/log.h"
#include "../core/unicode.h"
#include "../core/path_util.h"

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Find executable on PATH; returns heap path or NULL. */
static char *find_exe(ShellContext *ctx, const char *name) {
    static const char *EXT[] = { "", ".exe", ".cmd", ".bat", NULL };

    /* Absolute or relative with extension */
    if (strchr(name, '\\') || strchr(name, '/')) {
        for (int e = 0; EXT[e]; e++) {
            char full[MAX_PATH]; _snprintf(full, MAX_PATH, "%s%s", name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) return str_dup(full);
        }
        return NULL;
    }

    /* First search the directory that contains Wsh.exe.  This makes dist\ls.exe,
     * dist\tree.exe, etc. available even when PATH has not been updated yet. */
    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *bs = strrchr(exe_dir, '\\');
    if (bs) {
        *bs = '\0';
        for (int e = 0; EXT[e]; e++) {
            char full[MAX_PATH];
            _snprintf(full, MAX_PATH, "%s\\%s%s", exe_dir, name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) return str_dup(full);
        }
    }

    char pathenv[32768] = {0};
    GetEnvironmentVariableA("PATH", pathenv, sizeof(pathenv));
    char **dirs = NULL;
    int ndirs = str_split(pathenv, ';', &dirs);

    char *result = NULL;
    for (int d = 0; d < ndirs && !result; d++) {
        if (!dirs[d] || !dirs[d][0]) continue;
        for (int e = 0; EXT[e] && !result; e++) {
            char full[MAX_PATH];
            _snprintf(full, MAX_PATH, "%s\\%s%s", dirs[d], name, EXT[e]);
            if (path_exists(full) && !path_is_dir(full)) result = str_dup(full);
        }
    }
    str_split_free(dirs, ndirs);
    (void)ctx;
    return result;
}

/* ── Redirection ──────────────────────────────────────────────────────────── */


static HANDLE ctx_get_fd_handle(ShellContext *ctx, int fd) {
    if (fd == 0) return ctx->h_stdin;
    if (fd == 2) return ctx->h_stderr;
    return ctx->h_stdout;
}

static void ctx_replace_fd_handle(ShellContext *ctx, int fd, HANDLE h,
                                  HANDLE saved_in, HANDLE saved_out, HANDLE saved_err) {
    if (fd == 0) {
        if (ctx->h_stdin != saved_in) CloseHandle(ctx->h_stdin);
        ctx->h_stdin = h;
    } else if (fd == 2) {
        if (ctx->h_stderr != saved_err) CloseHandle(ctx->h_stderr);
        ctx->h_stderr = h;
    } else {
        if (ctx->h_stdout != saved_out) CloseHandle(ctx->h_stdout);
        ctx->h_stdout = h;
    }
}


bool exec_apply_redirs(ShellContext *ctx, Redir *redirs,
                       HANDLE *saved_in, HANDLE *saved_out, HANDLE *saved_err) {
    /* We store I/O state in the IShellIO but for file redirections we need
     * actual Win32 handles.  Store them in side-channel fields.
     * Since IShellIO is an interface, we use a concrete sub-type that exposes
     * the handles.  For simplicity here, redirections affect a secondary
     * handle set on the context used only for spawning children. */
    *saved_in  = INVALID_HANDLE_VALUE;
    *saved_out = INVALID_HANDLE_VALUE;
    *saved_err = INVALID_HANDLE_VALUE;

    /* ctx->h_stdin/out/err are the "current" child-process I/O handles */
    *saved_in  = ctx->h_stdin;
    *saved_out = ctx->h_stdout;
    *saved_err = ctx->h_stderr;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    for (Redir *r = redirs; r; r = r->next) {
        char *target = r->target ? expand_string(ctx, r->target) : NULL;
        const char *win_target = target;
        if (target && (!strcmp(target, "/dev/null") || !strcmp(target, "\\dev\\null"))) {
            win_target = "NUL";
        }
        switch (r->kind) {
            case REDIR_IN: {
                if (!target) goto err;
                wchar_t *wt = u8_to_u16(win_target, NULL);
                HANDLE h = CreateFileW(wt, GENERIC_READ, FILE_SHARE_READ,
                                       &sa, OPEN_EXISTING, 0, NULL);
                str_free(wt);
                if (h == INVALID_HANDLE_VALUE) {
                    WSH_LOG_WARN("Cannot open '%s' for reading", target);
                    str_free(target); goto err;
                }
                if (ctx->h_stdin != *saved_in) CloseHandle(ctx->h_stdin);
                ctx->h_stdin = h;
                break;
            }
            case REDIR_OUT:
            case REDIR_APPEND: {
                if (!target) goto err;
                wchar_t *wt = u8_to_u16(win_target, NULL);
                DWORD how = (r->kind == REDIR_APPEND) ? OPEN_ALWAYS : CREATE_ALWAYS;
                HANDLE h = CreateFileW(wt, GENERIC_WRITE, FILE_SHARE_READ,
                                       &sa, how, 0, NULL);
                str_free(wt);
                if (h == INVALID_HANDLE_VALUE) {
                    WSH_LOG_WARN("Cannot open '%s' for writing", target);
                    str_free(target); goto err;
                }
                if (r->kind == REDIR_APPEND)
                    SetFilePointer(h, 0, NULL, FILE_END);
                ctx_replace_fd_handle(ctx, r->fd, h, *saved_in, *saved_out, *saved_err);
                break;
            }
            case REDIR_DUP: {
                HANDLE src = ctx_get_fd_handle(ctx, r->target_fd);
                HANDLE dup = INVALID_HANDLE_VALUE;
                if (!DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(),
                                     &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                    WSH_LOG_WARN("Cannot duplicate fd %d to fd %d", r->target_fd, r->fd);
                    goto err;
                }
                ctx_replace_fd_handle(ctx, r->fd, dup, *saved_in, *saved_out, *saved_err);
                break;
            }
            default: break;
        }
        str_free(target);
    }
    return true;

err:
    exec_restore_redirs(ctx, *saved_in, *saved_out, *saved_err);
    return false;
}

void exec_restore_redirs(ShellContext *ctx,
                         HANDLE saved_in, HANDLE saved_out, HANDLE saved_err) {
    if (ctx->h_stdin  != saved_in  && ctx->h_stdin  != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->h_stdin);
    if (ctx->h_stdout != saved_out && ctx->h_stdout != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->h_stdout);
    if (ctx->h_stderr != saved_err && ctx->h_stderr != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->h_stderr);
    ctx->h_stdin  = saved_in;
    ctx->h_stdout = saved_out;
    ctx->h_stderr = saved_err;
}

/* ── External process spawn ───────────────────────────────────────────────── */

static bool handle_is_usable(HANDLE h) {
    if (h == NULL || h == INVALID_HANDLE_VALUE) return false;
    SetLastError(ERROR_SUCCESS);
    DWORD t = GetFileType(h);
    if (t == FILE_TYPE_UNKNOWN && GetLastError() != ERROR_SUCCESS) return false;
    return true;
}

static void append_quoted_arg(char *out, int out_size, int *pos, const char *arg) {
    if (!out || !pos || *pos >= out_size - 1) return;
    if (!arg) arg = "";
    bool q = !arg[0] || strchr(arg, ' ') || strchr(arg, '\t') || strchr(arg, '"');
    if (q && *pos < out_size - 1) out[(*pos)++] = '"';
    for (const char *p = arg; *p && *pos < out_size - 2; ++p) {
        if (*p == '"' && *pos < out_size - 2) out[(*pos)++] = '\\';
        out[(*pos)++] = *p;
    }
    if (q && *pos < out_size - 1) out[(*pos)++] = '"';
    out[*pos] = '\0';
}

static void forward_pipe_to_io(ShellContext *ctx, HANDLE hread, HANDLE hprocess) {
    char buf[4096];
    for (;;) {
        if (ctx->cancel_requested) {
            TerminateProcess(hprocess, 1);
            break;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(hread, NULL, 0, NULL, &available, NULL)) break;
        if (available > 0) {
            DWORD to_read = available > sizeof(buf) ? sizeof(buf) : available;
            DWORD got = 0;
            if (!ReadFile(hread, buf, to_read, &got, NULL) || got == 0) break;
            if (ctx->io && ctx->io->write) {
                int out_len = 0;
                char *out = wsh_bytes_to_utf8_for_terminal(buf, (int)got, &out_len);
                ctx->io->write(ctx->io, out ? out : buf, out ? out_len : (int)got);
                str_free(out);
            }
            continue;
        }
        DWORD wait = WaitForSingleObject(hprocess, 15);
        if (ctx->cancel_requested) {
            TerminateProcess(hprocess, 1);
            break;
        }
        if (wait == WAIT_OBJECT_0) {
            while (PeekNamedPipe(hread, NULL, 0, NULL, &available, NULL) && available > 0) {
                DWORD to_read = available > sizeof(buf) ? sizeof(buf) : available;
                DWORD got = 0;
                if (!ReadFile(hread, buf, to_read, &got, NULL) || got == 0) break;
                if (ctx->io && ctx->io->write) {
                    int out_len = 0;
                    char *out = wsh_bytes_to_utf8_for_terminal(buf, (int)got, &out_len);
                    ctx->io->write(ctx->io, out ? out : buf, out ? out_len : (int)got);
                    str_free(out);
                }
            }
            break;
        }
    }
}

static int spawn_external(ShellContext *ctx, char **argv, int argc, bool bg) {
    if (argc < 1 || !argv[0]) return 127;

    char *exe = find_exe(ctx, argv[0]);
    if (!exe) {
        char msg[512]; _snprintf(msg, sizeof(msg), "wsh: %s: command not found", argv[0]);
        io_writeln(ctx->io, msg);
        WSH_LOG_WARN("command not found: %s", argv[0]);
        return 127;
    }

    /* Build a quoted command line string.  Use the resolved executable path as
     * argv[0]; otherwise CreateProcessW may fail when Wsh.exe was launched
     * without dist in PATH. */
    char cmdline[32768]; int ci = 0;
    append_quoted_arg(cmdline, sizeof(cmdline), &ci, exe);
    for (int i = 1; i < argc && ci < 32760; i++) {
        if (ci < (int)sizeof(cmdline) - 1) cmdline[ci++] = ' ';
        append_quoted_arg(cmdline, sizeof(cmdline), &ci, argv[i]);
    }
    cmdline[ci] = '\0';
    str_free(exe);

    wchar_t *wcmd = u8_to_u16(cmdline, NULL);
    wchar_t *wcwd = u8_to_u16(ctx->cwd[0] ? ctx->cwd : ".", NULL);
    if (!wcmd) { str_free(wcwd); return 1; }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE cap_read = NULL, cap_write = NULL;
    bool capture = !bg && ctx->io && ctx->io->write && !handle_is_usable(ctx->h_stdout);
    if (capture) {
        if (!CreatePipe(&cap_read, &cap_write, &sa, 65536)) {
            WSH_LOG_WARN("CreatePipe failed for external command capture");
            capture = false;
        } else {
            SetHandleInformation(cap_read, HANDLE_FLAG_INHERIT, 0);
        }
    }

    HANDLE nul_in = NULL;
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = handle_is_usable(ctx->h_stdin) ? ctx->h_stdin : GetStdHandle(STD_INPUT_HANDLE);
    if (!handle_is_usable(si.hStdInput)) {
        nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        si.hStdInput = nul_in;
    }
    si.hStdOutput = capture ? cap_write : ctx->h_stdout;
    si.hStdError  = capture ? cap_write : ctx->h_stderr;

    DWORD creation_flags =
    CREATE_UNICODE_ENVIRONMENT |
    CREATE_NO_WINDOW;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(
        NULL,
        wcmd,
        NULL,
        NULL,
        TRUE,
        creation_flags,
        NULL,
        wcwd,
        &si,
        &pi
    );

    str_free(wcmd);
    str_free(wcwd);
    if (cap_write) CloseHandle(cap_write);
    if (nul_in) CloseHandle(nul_in);

    if (!ok) {
        WSH_LOG_ERROR("CreateProcessW failed for: %s", cmdline);
        wsh_log_win32("CreateProcessW (external)");
        if (cap_read) CloseHandle(cap_read);
        char msg[512]; _snprintf(msg, sizeof(msg), "wsh: %s: exec failed", argv[0]);
        io_writeln(ctx->io, msg);
        return 126;
    }
    CloseHandle(pi.hThread);

    if (bg) {
        job_add(&ctx->jobs, pi.dwProcessId, pi.hProcess, cmdline);
        ctx->last_bg_pid = pi.dwProcessId;
        char msg[64]; _snprintf(msg, sizeof(msg), "[%d] %lu",
                                 ctx->jobs.count, (unsigned long)pi.dwProcessId);
        io_writeln(ctx->io, msg);
        if (cap_read) CloseHandle(cap_read);
        return 0;
    }

    if (capture && cap_read) forward_pipe_to_io(ctx, cap_read, pi.hProcess);
    else {
        for (;;) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 50);
            if (wait == WAIT_OBJECT_0) break;
            if (ctx->cancel_requested) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (cap_read) CloseHandle(cap_read);
    return (int)code;
}


static bool alias_value_starts_with_name(const char *value, const char *name) {
    if (!value || !name) return false;
    while (*value == ' ' || *value == '\t') value++;
    size_t n = strlen(name);
    if (strncmp(value, name, n) != 0) return false;
    char c = value[n];
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* ── Execute NODE_CMD ─────────────────────────────────────────────────────── */

static int exec_cmd_node(ShellContext *ctx, ASTNode *node, bool bg) {
    if (!node || node->cmd.argc == 0) return 0;

    /* Save I/O for potential redirection */
    HANDLE si, so, se;
    if (!exec_apply_redirs(ctx, node->cmd.redirs, &si, &so, &se)) return 1;

    /* Expand all arguments.
     * Disable IFS splitting for words that contain '=' (assignments/alias values)
     * so that quoted spaces inside values are preserved intact. */
    char *eargv[1024]; int eargc = 0;
    for (int i = 0; i < node->cmd.argc && eargc < 1022; i++) {
        const char *arg = node->cmd.argv[i];
        bool has_eq = strchr(arg, '=') != NULL;
        WordList wl = expand_word(ctx, arg, !has_eq, !has_eq);
        for (int j = 0; j < wl.count && eargc < 1022; j++) {
            eargv[eargc++] = wl.words[j];
            wl.words[j]   = NULL; /* ownership transferred */
        }
        wl.count = 0; wordlist_free(&wl);
    }
    eargv[eargc] = NULL;

    /* xtrace */
    if (ctx->opts.xtrace && eargc > 0) {
        io_write(ctx->io, "+ ");
        for (int i = 0; i < eargc; i++) {
            if (i) io_write(ctx->io, " ");
            io_write(ctx->io, eargv[i]);
        }
        io_writeln(ctx->io, "");
    }

    int ret = 0;

    /* Pure assignment: if every argv word is NAME=VALUE, set env vars and return */
    if (eargc > 0) {
        bool all_assign = true;
        for (int i = 0; i < eargc && all_assign; i++) {
            const char *eq = strchr(eargv[i], '=');
            if (!eq || eq == eargv[i]) { all_assign = false; break; }
            for (const char *p = eargv[i]; p != eq; p++) {
                if (!isalnum((unsigned char)*p) && *p != '_') { all_assign = false; break; }
            }
        }
        if (all_assign) {
            for (int i = 0; i < eargc; i++) {
                char *eq = strchr(eargv[i], '=');
                if (!eq) continue;
                char name[256] = {0};
                size_t nl = (size_t)(eq - eargv[i]);
                if (nl > 255) nl = 255;
                strncpy(name, eargv[i], nl);
                env_set(ctx->env, name, eq + 1, false);
            }
            for (int i = 0; i < eargc; i++) str_free(eargv[i]);
            exec_restore_redirs(ctx, si, so, se);
            return 0;
        }
    }

    if (eargc > 0) {
        /* Resolve aliases. zsh suppresses recursive re-expansion of the
         * same alias during one expansion. Wsh used to recurse forever on
         * alias ls="ls" from the default .zshrc. Skip immediate self aliases
         * and guard against alias loops such as a=b; b=a. */
        if (!ctx->suppress_alias) {
            for (Alias *a = ctx->aliases; a; a = a->next) {
                if (strcmp(a->name, eargv[0]) == 0) {
                    if (alias_value_starts_with_name(a->value, a->name)) {
                        break; /* self-referential alias: run command normally */
                    }
                    if (ctx->alias_depth >= 32) {
                        io_writeln(ctx->io, "wsh: alias expansion depth exceeded");
                        ret = 1;
                        goto done;
                    }

                    char combined[8192];
                    _snprintf(combined, sizeof(combined), "%s", a->value);
                    if (eargc > 1) {
                        strncat(combined, " ", sizeof(combined) - strlen(combined) - 1);
                        for (int i = 1; i < eargc; i++) {
                            if (i > 1) strncat(combined, " ", sizeof(combined) - strlen(combined) - 1);
                            strncat(combined, eargv[i], sizeof(combined) - strlen(combined) - 1);
                        }
                    }
                    ctx->alias_depth++;
                    ret = shell_exec_line(ctx, combined);
                    ctx->alias_depth--;
                    goto done;
                }
            }
        }

        /* Shell functions */
        for (ShellFunc *f = ctx->functions; f; f = f->next) {
            if (strcmp(f->name, eargv[0]) == 0) {
                EnvScope *old_env = ctx->env;
                ctx->env = env_scope_push(old_env);
                for (int i = 1; i < eargc; i++) {
                    char idx[8]; _snprintf(idx, sizeof(idx), "%d", i);
                    env_set(ctx->env, idx, eargv[i], false);
                }
                ctx->call_depth++;
                ret = exec_node(ctx, f->body);
                ctx->call_depth--;
                ctx->env = env_scope_pop(ctx->env);
                ctx->env = old_env;
                goto done;
            }
        }

        /* Built-ins */
        BuiltinFn fn = builtin_find(eargv[0]);
        if (fn) { ret = fn(eargc, eargv, ctx); goto done; }

        /* External */
        ret = spawn_external(ctx, eargv, eargc, bg);
    }

done:
    for (int i = 0; i < eargc; i++) str_free(eargv[i]);
    exec_restore_redirs(ctx, si, so, se);
    return ret;
}

/* ── Execute pipe: left | right ───────────────────────────────────────────── */

static int exec_pipe(ShellContext *ctx, ASTNode *node) {
    HANDLE hread, hwrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hread, &hwrite, &sa, 65536)) return 1;

    /* Left → writes to hwrite */
    HANDLE old_out = ctx->h_stdout;
    ctx->h_stdout  = hwrite;
    exec_node(ctx, node->binary.left);
    ctx->h_stdout  = old_out;
    CloseHandle(hwrite);

    /* Right ← reads from hread */
    HANDLE old_in = ctx->h_stdin;
    ctx->h_stdin  = hread;
    int ret = exec_node(ctx, node->binary.right);
    ctx->h_stdin  = old_in;
    CloseHandle(hread);

    return ret;
}

/* ── Main dispatcher (Visitor over NodeKind) ──────────────────────────────── */

int exec_node(ShellContext *ctx, ASTNode *node) {
    if (!node || ctx->exit_requested) return ctx->exit_code;

    int ret = 0;

    switch (node->kind) {
        case NODE_CMD:
            ret = exec_cmd_node(ctx, node, false);
            break;

        case NODE_PIPE:
            ret = exec_pipe(ctx, node);
            break;

        case NODE_AND:
            ret = exec_node(ctx, node->binary.left);
            if (ret == 0 && !ctx->exit_requested)
                ret = exec_node(ctx, node->binary.right);
            break;

        case NODE_OR:
            ret = exec_node(ctx, node->binary.left);
            if (ret != 0 && !ctx->exit_requested)
                ret = exec_node(ctx, node->binary.right);
            break;

        case NODE_SEQ:
            ret = exec_node(ctx, node->binary.left);
            if (!ctx->exit_requested)
                ret = exec_node(ctx, node->binary.right);
            break;

        case NODE_BG:
            if (node->wrap.child && node->wrap.child->kind == NODE_CMD)
                ret = exec_cmd_node(ctx, node->wrap.child, true);
            else
                ret = exec_node(ctx, node->wrap.child);
            break;

        case NODE_NEGATE:
            ret = exec_node(ctx, node->wrap.child);
            ret = ret ? 0 : 1;
            break;

        case NODE_GROUP:
        case NODE_SUBSHELL:
            /* TODO: NODE_SUBSHELL should fork; for now run in same context */
            ret = exec_node(ctx, node->wrap.child);
            break;

        case NODE_IF: {
            ret = exec_node(ctx, node->ifnode.cond);
            if (ret == 0)
                ret = exec_node(ctx, node->ifnode.body);
            else if (node->ifnode.alt)
                ret = exec_node(ctx, node->ifnode.alt);
            else
                ret = 0;
            break;
        }

        case NODE_WHILE:
            while (!ctx->exit_requested) {
                ret = exec_node(ctx, node->loop.cond);
                if (ret != 0) break;
                ret = exec_node(ctx, node->loop.body);
            }
            break;

        case NODE_UNTIL:
            while (!ctx->exit_requested) {
                ret = exec_node(ctx, node->loop.cond);
                if (ret == 0) break;
                ret = exec_node(ctx, node->loop.body);
            }
            break;

        case NODE_FOR: {
            for (int i = 0; i < node->fornode.word_count && !ctx->exit_requested; i++) {
                char *val = expand_string(ctx, node->fornode.words[i]);
                env_set(ctx->env, node->fornode.var, val, false);
                str_free(val);
                ret = exec_node(ctx, node->fornode.body);
            }
            break;
        }

        case NODE_CASE: {
            char *word = expand_string(ctx, node->casenode.word);
            ret = 0;
            for (int i = 0; i < node->casenode.count; i++) {
                CaseArm *arm = &node->casenode.arms[i];
                bool matched = false;
                for (int j = 0; j < arm->pat_count && !matched; j++) {
                    const char *pat = arm->patterns[j];
                    if (!pat) continue;
                    /* Expand the pattern, then match */
                    char *epat = expand_string(ctx, pat);
                    /* Use simple glob match: * matches any, ? matches one */
                    wchar_t *wpat  = u8_to_u16(epat,  NULL);
                    wchar_t *wword = u8_to_u16(word, NULL);
                    if (wpat && wword) matched = !!PathMatchSpecW(wword, wpat);
                    /* fallback: exact match */
                    if (!matched && strcmp(epat, word) == 0) matched = true;
                    str_free(wpat); str_free(wword); str_free(epat);
                }
                if (matched) {
                    if (arm->body) ret = exec_node(ctx, arm->body);
                    break;
                }
            }
            str_free(word);
            ctx->last_status = ret;
            break;
        }

        case NODE_FUNCTION: {
            /* Register function: replace if already exists */
            for (ShellFunc *f = ctx->functions; f; f = f->next) {
                if (strcmp(f->name, node->func.name) == 0) {
                    f->body = node->func.body; /* arena owns the body */
                    ctx->preserve_ast_arena = true;
                    ret = 0; goto done;
                }
            }
            ShellFunc *fn = (ShellFunc *)HeapAlloc(GetProcessHeap(),
                                                    HEAP_ZERO_MEMORY, sizeof(ShellFunc));
            fn->name     = str_dup(node->func.name);
            fn->body     = node->func.body;
            fn->next     = ctx->functions;
            ctx->functions = fn;
            ctx->preserve_ast_arena = true;
            ret = 0;
            break;
        }

        case NODE_ASSIGN: {
            char *val = expand_string(ctx, node->assign.value);
            env_set(ctx->env, node->assign.name, val, false);
            str_free(val);
            ret = 0;
            break;
        }

        case NODE_ARITH:
            expand_arith(ctx, node->arith.expr);
            ret = 0;
            break;

        default:
            WSH_LOG_WARN("exec_node: unhandled NodeKind %d", node->kind);
            ret = 1;
            break;
    }

done:
    ctx->last_status = ret;
    if (ctx->opts.err_exit && ret != 0 && !ctx->exit_requested)
        ctx->exit_requested = true;
    return ret;
}
