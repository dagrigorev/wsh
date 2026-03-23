#pragma once
/*
 * expand.h — ZSH-compatible word expansion pipeline.
 *
 * Expansion order follows POSIX / ZSH:
 *   1. History expansion  (!!, !n, !str)
 *   2. Tilde expansion    (~, ~/path)
 *   3. Parameter / variable expansion  ($VAR, ${VAR:-def}, ${#VAR} …)
 *   4. Command substitution  $(cmd), `cmd`
 *   5. Arithmetic expansion  $((expr))
 *   6. Word splitting (IFS)
 *   7. Brace expansion  {a,b,c}, {1..5}
 *   8. Pathname expansion (globbing)  *, ?, [class], **
 *   9. Quote removal
 *
 * Interface Segregation: each expansion phase is exposed as its own function
 * so tests can exercise them independently.
 */
#ifndef WSH_EXPAND_H
#define WSH_EXPAND_H

#include <stdbool.h>

/* Forward-declare to avoid circular includes */
typedef struct ShellContext ShellContext;

/* ── Word list (result of expansion) ─────────────────────────────────────── */

typedef struct {
    char **words;   /* heap-allocated array of heap-allocated strings */
    int    count;
    int    cap;
} WordList;

void wordlist_init(WordList *wl);
void wordlist_push(WordList *wl, char *word); /* takes ownership of word */
void wordlist_free(WordList *wl);

/* ── Individual expansion phases ─────────────────────────────────────────── */

/* Expand a single word through the full pipeline.
 * If glob_ok=false, skip glob expansion.
 * If split_ok=false, skip IFS word splitting.
 * Caller must call wordlist_free() on result. */
WordList expand_word(ShellContext *ctx, const char *word,
                     bool glob_ok, bool split_ok);

/* Expand variable/command-substitution/arithmetic in a string.
 * Result is a single heap-allocated string. Caller frees. */
char *expand_string(ShellContext *ctx, const char *s);

/* Expand a tilde prefix only. Caller frees. */
char *expand_tilde(ShellContext *ctx, const char *s);

/* Evaluate an arithmetic expression. */
long  expand_arith(ShellContext *ctx, const char *expr);

/* Brace expansion: returns a WordList (no further expansion applied). */
WordList expand_brace(const char *word);

/* Glob expansion: returns matching paths or the original pattern if no match. */
WordList expand_glob(const char *pattern);

/* Split s on IFS characters into a WordList. */
WordList expand_split_ifs(ShellContext *ctx, const char *s);

#endif /* WSH_EXPAND_H */
