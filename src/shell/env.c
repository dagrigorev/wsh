#include <windows.h>
#include <string.h>
#include "env.h"
#include "../core/str_util.h"
#include "../core/log.h"

EnvScope *env_scope_push(EnvScope *parent) {
    EnvScope *s = (EnvScope *)HeapAlloc(GetProcessHeap(),
                                         HEAP_ZERO_MEMORY, sizeof(EnvScope));
    if (s) s->parent = parent;
    return s;
}

EnvScope *env_scope_pop(EnvScope *top) {
    if (!top) return NULL;
    EnvScope *parent = top->parent;
    /* Free all variables in this scope */
    EnvVar *v = top->vars;
    while (v) {
        EnvVar *next = v->next;
        str_free(v->name);
        str_free(v->value);
        HeapFree(GetProcessHeap(), 0, v);
        v = next;
    }
    HeapFree(GetProcessHeap(), 0, top);
    return parent;
}

const char *env_get(const EnvScope *scope, const char *name) {
    if (!name) return NULL;
    for (const EnvScope *s = scope; s; s = s->parent) {
        for (const EnvVar *v = s->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) return v->value;
        }
    }
    /* Fall back to Win32 process environment */
    static char envbuf[8192];
    DWORD n = GetEnvironmentVariableA(name, envbuf, sizeof(envbuf));
    return n ? envbuf : NULL;
}

void env_set(EnvScope *scope, const char *name, const char *value, bool exported) {
    if (!scope || !name) return;

    /* Search top scope for existing entry */
    for (EnvVar *v = scope->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (v->readonly) {
                WSH_LOG_WARN("env_set: %s is readonly", name);
                return;
            }
            str_free(v->value);
            v->value    = str_dup(value ? value : "");
            if (exported) v->exported = true;
            if (v->exported) SetEnvironmentVariableA(name, v->value);
            return;
        }
    }

    /* Create new entry */
    EnvVar *v = (EnvVar *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EnvVar));
    v->name     = str_dup(name);
    v->value    = str_dup(value ? value : "");
    v->exported = exported;
    v->next     = scope->vars;
    scope->vars = v;

    if (exported) SetEnvironmentVariableA(name, v->value);
}

void env_unset(EnvScope *scope, const char *name) {
    for (EnvScope *s = scope; s; s = s->parent) {
        EnvVar **pp = &s->vars;
        while (*pp) {
            if (strcmp((*pp)->name, name) == 0) {
                EnvVar *dead = *pp;
                *pp = dead->next;
                if (dead->exported) SetEnvironmentVariableA(name, NULL);
                str_free(dead->name);
                str_free(dead->value);
                HeapFree(GetProcessHeap(), 0, dead);
                return;
            }
            pp = &(*pp)->next;
        }
    }
}

void env_import_process(EnvScope *global_scope) {
    /*
     * Walk the Win32 environment block and import every variable into the
     * top scope as exported.  This ensures $PATH, $USERPROFILE, etc. are
     * visible to shell expansions immediately.
     */
    wchar_t *block = GetEnvironmentStringsW();
    if (!block) return;

    for (const wchar_t *p = block; *p; p += wcslen(p) + 1) {
        char *line = u16_to_u8(p, NULL);
        if (!line) continue;
        char *eq = strchr(line, '=');
        if (eq && eq != line) { /* skip =ExitCode style system vars */
            *eq = '\0';
            env_set(global_scope, line, eq + 1, /*exported=*/true);
        }
        str_free(line);
    }
    FreeEnvironmentStringsW(block);
}
