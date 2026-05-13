#pragma once
#ifndef WSH_EXPAND_H
#define WSH_EXPAND_H

#include <windows.h>
#include "wsh_bool.h"
#include "util.h"

/* Forward-declare ShellContext to avoid circular include */
typedef struct ShellContext ShellContext;

/* ─── Word expansion result ─────────────────────────────────────────────── */

typedef struct {
    char **words;
    int    count;
    int    cap;
} WordList;

void wordlist_init(WordList *wl);
void wordlist_push(WordList *wl, char *word);   /* takes ownership */
void wordlist_free(WordList *wl);

/* ─── API ────────────────────────────────────────────────────────────────── */

/* Perform all expansions on a single word token.
   Returns a WordList (may have multiple words from glob/brace expansion).
   Caller must call wordlist_free on result. */
WordList expand_word(ShellContext *ctx, const char *word, bool glob_ok, bool split_ok);

/* Expand a string for display only (no glob, no split) */
char *expand_string(ShellContext *ctx, const char *s);

/* Tilde expansion only */
char *expand_tilde(ShellContext *ctx, const char *s);

/* Variable expansion: ${VAR}, ${VAR:-default}, ${#VAR}, etc. */
char *expand_variable(ShellContext *ctx, const char *s);

/* Command substitution: $(cmd) or `cmd` */
char *expand_command_subst(ShellContext *ctx, const char *cmd);

/* Arithmetic expansion: $((expr)) */
long expand_arith(ShellContext *ctx, const char *expr);
char *expand_arith_str(ShellContext *ctx, const char *expr);

/* Brace expansion: {a,b,c} {1..5} — returns word list */
WordList expand_brace(const char *s);

/* Glob expansion: *, ?, [chars], ** — uses FindFirstFileW */
WordList expand_glob(const char *pattern);

/* IFS word split */
WordList split_ifs(ShellContext *ctx, const char *s);

#endif /* WSH_EXPAND_H */
