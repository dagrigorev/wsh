#pragma once
/*
 * builtins.h — Built-in command registry.
 *
 * Open/Closed Principle: new built-ins are added by appending to the
 * BUILTIN_TABLE in builtins.c.  The dispatch function builtin_find() does
 * not change.
 *
 * Each built-in has the signature:
 *   int fn(int argc, char **argv, ShellContext *ctx)
 * matching the external-command convention so callers don't need to
 * distinguish built-ins from functions from external commands.
 */
#ifndef WSH_BUILTINS_H
#define WSH_BUILTINS_H

#include "shell_ctx.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef int (*BuiltinFn)(int argc, char **argv, ShellContext *ctx);

/* Find a built-in by name; returns NULL if not found. */
BuiltinFn builtin_find(const char *name);

/* ── Built-in declarations ───────────────────────────────────────────────── */
int builtin_cd(int argc, char **argv, ShellContext *ctx);
int builtin_echo(int argc, char **argv, ShellContext *ctx);
int builtin_printf_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_export(int argc, char **argv, ShellContext *ctx);
int builtin_unset(int argc, char **argv, ShellContext *ctx);
int builtin_alias(int argc, char **argv, ShellContext *ctx);
int builtin_unalias(int argc, char **argv, ShellContext *ctx);
int builtin_source(int argc, char **argv, ShellContext *ctx);
int builtin_exit(int argc, char **argv, ShellContext *ctx);
int builtin_return_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_true_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_false_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_test(int argc, char **argv, ShellContext *ctx);
int builtin_read(int argc, char **argv, ShellContext *ctx);
int builtin_set_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_setopt(int argc, char **argv, ShellContext *ctx);
int builtin_jobs(int argc, char **argv, ShellContext *ctx);
int builtin_fg(int argc, char **argv, ShellContext *ctx);
int builtin_bg(int argc, char **argv, ShellContext *ctx);
int builtin_kill_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_wait_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_pwd(int argc, char **argv, ShellContext *ctx);
int builtin_type(int argc, char **argv, ShellContext *ctx);
int builtin_which(int argc, char **argv, ShellContext *ctx);
int builtin_eval(int argc, char **argv, ShellContext *ctx);
int builtin_exec(int argc, char **argv, ShellContext *ctx);
int builtin_local(int argc, char **argv, ShellContext *ctx);
int builtin_typeset(int argc, char **argv, ShellContext *ctx);
int builtin_hash(int argc, char **argv, ShellContext *ctx);
int builtin_trap(int argc, char **argv, ShellContext *ctx);
int builtin_open(int argc, char **argv, ShellContext *ctx);
int builtin_clip(int argc, char **argv, ShellContext *ctx);
int builtin_env_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_sudo(int argc, char **argv, ShellContext *ctx);
/* Scheduler commands */
int builtin_at(int argc, char **argv, ShellContext *ctx);
int builtin_atq(int argc, char **argv, ShellContext *ctx);
int builtin_atrm(int argc, char **argv, ShellContext *ctx);

/* ZSH completion stubs (no-ops that prevent "command not found" errors) */
int builtin_noop(int argc, char **argv, ShellContext *ctx);


#ifdef __cplusplus
}
#endif

#endif /* WSH_BUILTINS_H */
