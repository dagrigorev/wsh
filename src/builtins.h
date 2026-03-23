#pragma once
#ifndef WSH_BUILTINS_H
#define WSH_BUILTINS_H

#include <stdbool.h>
#include "shell.h"

/* Signature for all built-in handlers */
typedef int (*BuiltinFn)(int argc, char **argv, ShellContext *ctx);

typedef struct {
    const char *name;
    BuiltinFn   fn;
} BuiltinEntry;

/* Returns the handler for a built-in name, or NULL if not a builtin */
BuiltinFn builtin_find(const char *name);

/* All individual built-in implementations */
int builtin_cd(int argc, char **argv, ShellContext *ctx);
int builtin_echo(int argc, char **argv, ShellContext *ctx);
int builtin_printf_cmd(int argc, char **argv, ShellContext *ctx);
int builtin_export(int argc, char **argv, ShellContext *ctx);
int builtin_unset(int argc, char **argv, ShellContext *ctx);
int builtin_alias(int argc, char **argv, ShellContext *ctx);
int builtin_unalias(int argc, char **argv, ShellContext *ctx);
int builtin_source(int argc, char **argv, ShellContext *ctx);
int builtin_exit(int argc, char **argv, ShellContext *ctx);
int builtin_return(int argc, char **argv, ShellContext *ctx);
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
int builtin_wait(int argc, char **argv, ShellContext *ctx);
int builtin_pwd(int argc, char **argv, ShellContext *ctx);
int builtin_type(int argc, char **argv, ShellContext *ctx);
int builtin_which(int argc, char **argv, ShellContext *ctx);
int builtin_eval(int argc, char **argv, ShellContext *ctx);
int builtin_exec(int argc, char **argv, ShellContext *ctx);
int builtin_local(int argc, char **argv, ShellContext *ctx);
int builtin_typeset(int argc, char **argv, ShellContext *ctx);
int builtin_arith(int argc, char **argv, ShellContext *ctx);
int builtin_hash(int argc, char **argv, ShellContext *ctx);
int builtin_trap(int argc, char **argv, ShellContext *ctx);
int builtin_autoload(int argc, char **argv, ShellContext *ctx);
int builtin_compdef(int argc, char **argv, ShellContext *ctx);
int builtin_zstyle(int argc, char **argv, ShellContext *ctx);
int builtin_compinit(int argc, char **argv, ShellContext *ctx);
int builtin_zle(int argc, char **argv, ShellContext *ctx);
/* Windows-specific */
int builtin_open(int argc, char **argv, ShellContext *ctx);
int builtin_clip(int argc, char **argv, ShellContext *ctx);
int builtin_env(int argc, char **argv, ShellContext *ctx);
int builtin_sudo(int argc, char **argv, ShellContext *ctx);

#endif /* WSH_BUILTINS_H */
