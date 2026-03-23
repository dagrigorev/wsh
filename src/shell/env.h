#pragma once
/*
 * env.h — Lexically-scoped environment variable store.
 *
 * Variables are stored in a singly-linked list of EnvScope frames.
 * Each function call or subshell pushes a new frame; returning pops it.
 * Lookup traverses from innermost outward — classic dynamic binding with
 * local-variable shadowing.
 *
 * Exported variables are also mirrored into the Win32 process environment
 * so that child processes created with CreateProcess inherit them without
 * needing an explicit environment block.
 *
 * SOLID:
 *   S — Only env var management; no shell logic.
 *   O — New variable attributes (array, integer) added without changing
 *       the core lookup/set functions.
 */
#ifndef WSH_ENV_H
#define WSH_ENV_H

#include <windows.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ── Variable attributes ──────────────────────────────────────────────────── */

typedef struct EnvVar {
    char         *name;
    char         *value;
    bool          exported;   /* visible to child processes */
    bool          readonly;   /* cannot be unset or reassigned */
    bool          integer;    /* value is always decimal integer */
    struct EnvVar *next;
} EnvVar;

/* ── Scope frame ──────────────────────────────────────────────────────────── */

typedef struct EnvScope {
    EnvVar          *vars;
    struct EnvScope *parent;  /* outer scope; NULL for global scope */
} EnvScope;

/* ── API ──────────────────────────────────────────────────────────────────── */

/* Allocate a new scope parented to 'parent' (may be NULL). */
EnvScope   *env_scope_push(EnvScope *parent);

/* Free the top scope; return its parent.  Caller must re-assign the pointer. */
EnvScope   *env_scope_pop(EnvScope *top);

/* Look up 'name' walking the scope chain; returns value or NULL. */
const char *env_get(const EnvScope *scope, const char *name);

/* Set 'name' = 'value' in the top scope (creates if absent).
 * If exported=true, also calls SetEnvironmentVariableA. */
void        env_set(EnvScope *scope, const char *name,
                    const char *value, bool exported);

/* Remove 'name' from the scope chain (first occurrence). */
void        env_unset(EnvScope *scope, const char *name);

/* Sync a subset of the Win32 process environment into the global scope.
 * Called once at startup so $PATH, $HOME etc. are visible. */
void        env_import_process(EnvScope *global_scope);


#ifdef __cplusplus
}
#endif

#endif /* WSH_ENV_H */
