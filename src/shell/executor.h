#pragma once
/*
 * executor.h — AST executor (Visitor pattern over NodeKind).
 *
 * The executor walks the AST produced by the parser and carries out each
 * node's semantics.  It is deliberately separate from the parser so that:
 *   - The AST can be inspected / pretty-printed without execution (SRP).
 *   - Alternative executors (e.g. a dry-run / syntax-check mode) can be
 *     swapped in without touching the parser (OCP).
 */
#ifndef WSH_EXECUTOR_H
#define WSH_EXECUTOR_H

#include "parser.h"
#include "shell_ctx.h"

/* Execute an AST node; returns its exit status. */
int exec_node(ShellContext *ctx, ASTNode *node);

/* Apply redirections attached to a command; save old I/O in *saved_*.
 * Returns false on error. */
bool exec_apply_redirs(ShellContext *ctx, Redir *redirs,
                       HANDLE *saved_in, HANDLE *saved_out, HANDLE *saved_err);

/* Restore I/O handles saved by exec_apply_redirs. */
void exec_restore_redirs(ShellContext *ctx,
                         HANDLE saved_in, HANDLE saved_out, HANDLE saved_err);

#endif /* WSH_EXECUTOR_H */
