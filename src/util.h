#pragma once
#ifndef WSH_UTIL_H
#define WSH_UTIL_H
#endif

/* POSIX compat for MSVC */
#ifndef strncasecmp
#  define strncasecmp _strnicmp
#endif
#ifndef strcasecmp
#  define strcasecmp  _stricmp
#endif

#ifndef WSH_UTIL_H
#define WSH_UTIL_H

#include <windows.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ─── Arena Allocator ────────────────────────────────────────────────────── */

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t             used;
    size_t             cap;
    char               data[1];
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
    size_t      block_size;
} Arena;

Arena *arena_create(size_t initial_size);
void  *arena_alloc(Arena *a, size_t size);
char  *arena_strdup(Arena *a, const char *s);
void   arena_reset(Arena *a);   /* free all blocks except first */
void   arena_destroy(Arena *a);

/* ─── UTF-8 / UTF-16 ─────────────────────────────────────────────────────── */

/* Returns heap-allocated wchar_t* — caller frees, or uses arena */
wchar_t *utf8_to_utf16(const char *s, Arena *a);
char    *utf16_to_utf8(const wchar_t *s, Arena *a);

/* Decode one UTF-8 codepoint from *p; advance *p; returns 0xFFFD on error */
uint32_t utf8_decode(const char **p);

/* Encode codepoint into buf (max 4 bytes); returns byte count */
int utf8_encode(uint32_t cp, char *buf);

/* ─── String Helpers ─────────────────────────────────────────────────────── */

char *str_dup(const char *s);
char *str_ndup(const char *s, size_t n);
char *str_join(char **parts, int n, const char *sep);
bool  str_startswith(const char *s, const char *prefix);
bool  str_endswith(const char *s, const char *suffix);

/* Split s on delim; fills *out (heap array of heap strings); returns count */
int str_split(const char *s, char delim, char ***out);
void str_split_free(char **parts, int n);

/* Strip leading/trailing whitespace in-place; returns s */
char *str_trim(char *s);

/* ─── Path Utilities ─────────────────────────────────────────────────────── */

char *path_expand_tilde(const char *path);
char *path_join(const char *a, const char *b);
bool  path_exists(const char *path);
bool  path_is_dir(const char *path);
char *path_basename(const char *path);
char *path_dirname(const char *path);

/* ─── Misc ───────────────────────────────────────────────────────────────── */

/* Log to debugger output */
void wsh_log(const char *fmt, ...);
void wsh_log_win32_error(const char *context);

/* Portable strdup wrapper (heap) */
#ifndef _strdup
#  define _strdup str_dup
#endif

#endif /* WSH_UTIL_H */
