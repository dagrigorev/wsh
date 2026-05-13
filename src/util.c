#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "wsh_bool.h"
#include <stdint.h>
#include "util.h"

/* ─── Arena Allocator ────────────────────────────────────────────────────── */

Arena *arena_create(size_t initial_size) {
    if (initial_size < 4096) initial_size = 4096;
    ArenaBlock *b = (ArenaBlock *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             offsetof(ArenaBlock, data) + initial_size);
    if (!b) return NULL;
    b->cap  = initial_size;
    b->used = 0;
    b->next = NULL;

    Arena *a = (Arena *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Arena));
    if (!a) { HeapFree(GetProcessHeap(), 0, b); return NULL; }
    a->head       = b;
    a->block_size = initial_size;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    ArenaBlock *b = a->head;
    if (!b || b->used + size > b->cap) {
        size_t bsz = size > a->block_size ? size : a->block_size;
        ArenaBlock *nb = (ArenaBlock *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                  offsetof(ArenaBlock, data) + bsz);
        if (!nb) return NULL;
        nb->cap  = bsz;
        nb->used = 0;
        nb->next = a->head;
        a->head  = nb;
        b        = nb;
    }
    void *ptr = b->data + b->used;
    b->used += size;
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char  *d   = (char *)arena_alloc(a, len);
    if (d) memcpy(d, s, len);
    return d;
}

void arena_reset(Arena *a) {
    /* Keep first block, free rest */
    ArenaBlock *b = a->head;
    if (!b) return;
    /* Walk to last */
    while (b->next) {
        ArenaBlock *dead = b;
        b = b->next;
        HeapFree(GetProcessHeap(), 0, dead);
    }
    a->head       = b;
    b->used       = 0;
    b->next       = NULL;
}

void arena_destroy(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        HeapFree(GetProcessHeap(), 0, b);
        b = next;
    }
    HeapFree(GetProcessHeap(), 0, a);
}

/* ─── UTF-8 / UTF-16 ─────────────────────────────────────────────────────── */

wchar_t *utf8_to_utf16(const char *s, Arena *a) {
    if (!s) return NULL;
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (needed <= 0) return NULL;
    wchar_t *out = a ? (wchar_t *)arena_alloc(a, (size_t)needed * sizeof(wchar_t))
                     : (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed * sizeof(wchar_t));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, needed);
    return out;
}

char *utf16_to_utf8(const wchar_t *s, Arena *a) {
    if (!s) return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char *out = a ? (char *)arena_alloc(a, (size_t)needed)
                  : (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, needed, NULL, NULL);
    return out;
}

uint32_t utf8_decode(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;

    if (*s < 0x80)       { cp = *s++;        extra = 0; }
    else if (*s < 0xC0)  { (*p)++;           return 0xFFFD; }
    else if (*s < 0xE0)  { cp = *s++ & 0x1F; extra = 1; }
    else if (*s < 0xF0)  { cp = *s++ & 0x0F; extra = 2; }
    else                  { cp = *s++ & 0x07; extra = 3; }

    for (int i = 0; i < extra; i++) {
        if ((*s & 0xC0) != 0x80) { *p = (const char *)s; return 0xFFFD; }
        cp = (cp << 6) | (*s++ & 0x3F);
    }
    *p = (const char *)s;
    return cp;
}

int utf8_encode(uint32_t cp, char *buf) {
    if (cp < 0x80)   { buf[0] = (char)cp; return 1; }
    if (cp < 0x800)  { buf[0] = (char)(0xC0 | (cp >> 6));  buf[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000){ buf[0] = (char)(0xE0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                       buf[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    buf[0] = (char)(0xF0 | (cp >> 18)); buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

/* ─── String Helpers ─────────────────────────────────────────────────────── */

char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)HeapAlloc(GetProcessHeap(), 0, n);
    if (d) memcpy(d, s, n);
    return d;
}

char *str_ndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *d = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *str_join(char **parts, int n, const char *sep) {
    if (!parts || n <= 0) return str_dup("");
    size_t sep_len = sep ? strlen(sep) : 0;
    size_t total   = 0;
    for (int i = 0; i < n; i++) {
        if (parts[i]) total += strlen(parts[i]);
        if (i < n - 1) total += sep_len;
    }
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, total + 1);
    if (!out) return NULL;
    char *p = out;
    for (int i = 0; i < n; i++) {
        if (parts[i]) { size_t l = strlen(parts[i]); memcpy(p, parts[i], l); p += l; }
        if (i < n - 1 && sep) { memcpy(p, sep, sep_len); p += sep_len; }
    }
    *p = '\0';
    return out;
}

bool str_startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_endswith(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s), lp = strlen(suffix);
    if (lp > ls) return false;
    return strcmp(s + ls - lp, suffix) == 0;
}

int str_split(const char *s, char delim, char ***out) {
    if (!s || !out) return 0;
    int count = 1;
    for (const char *p = s; *p; p++) if (*p == delim) count++;

    *out = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               (size_t)(count + 1) * sizeof(char *));
    if (!*out) return 0;

    int idx = 0;
    const char *start = s;
    for (const char *p = s; ; p++) {
        if (*p == delim || *p == '\0') {
            (*out)[idx++] = str_ndup(start, (size_t)(p - start));
            if (!*p) break;
            start = p + 1;
        }
    }
    (*out)[idx] = NULL;
    return idx;
}

void str_split_free(char **parts, int n) {
    if (!parts) return;
    for (int i = 0; i < n; i++) HeapFree(GetProcessHeap(), 0, parts[i]);
    HeapFree(GetProcessHeap(), 0, parts);
}

char *str_trim(char *s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
    return s;
}

/* ─── Path Utilities ─────────────────────────────────────────────────────── */

char *path_expand_tilde(const char *path) {
    if (!path) return NULL;
    if (path[0] != '~') return str_dup(path);

    char home[MAX_PATH] = {0};
    /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH */
    DWORD n = GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH);
    if (!n) {
        char drive[64] = {0}, hpath[MAX_PATH] = {0};
        GetEnvironmentVariableA("HOMEDRIVE", drive, sizeof(drive));
        GetEnvironmentVariableA("HOMEPATH",  hpath, sizeof(hpath));
        _snprintf(home, sizeof(home), "%s%s", drive, hpath);
    }
    if (!home[0]) return str_dup(path);

    const char *rest = path + 1;
    if (*rest == '\\' || *rest == '/' || *rest == '\0') {
        return path_join(home, (*rest ? rest + 1 : ""));
    }
    return str_dup(path); /* ~user form — not expanded for simplicity */
}

char *path_join(const char *a, const char *b) {
    if (!a || !a[0]) return str_dup(b ? b : "");
    if (!b || !b[0]) return str_dup(a);
    size_t la = strlen(a), lb = strlen(b);
    bool slash = (a[la-1] == '\\' || a[la-1] == '/');
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, la + lb + 2);
    if (!out) return NULL;
    memcpy(out, a, la);
    if (!slash) { out[la++] = '\\'; }
    memcpy(out + la, b, lb + 1);
    return out;
}

bool path_exists(const char *path) {
    if (!path) return false;
    wchar_t *w = utf8_to_utf16(path, NULL);
    if (!w) return false;
    DWORD attr = GetFileAttributesW(w);
    HeapFree(GetProcessHeap(), 0, w);
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool path_is_dir(const char *path) {
    if (!path) return false;
    wchar_t *w = utf8_to_utf16(path, NULL);
    if (!w) return false;
    DWORD attr = GetFileAttributesW(w);
    HeapFree(GetProcessHeap(), 0, w);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

char *path_basename(const char *path) {
    if (!path) return str_dup(".");
    const char *p = path + strlen(path);
    while (p > path && (p[-1] == '\\' || p[-1] == '/')) p--;
    const char *end = p;
    while (p > path && p[-1] != '\\' && p[-1] != '/') p--;
    if (p == end) return str_dup(".");
    return str_ndup(p, (size_t)(end - p));
}

char *path_dirname(const char *path) {
    if (!path) return str_dup(".");
    const char *p = path + strlen(path);
    while (p > path && (p[-1] == '\\' || p[-1] == '/')) p--;
    while (p > path && p[-1] != '\\' && p[-1] != '/') p--;
    while (p > path && (p[-1] == '\\' || p[-1] == '/')) p--;
    if (p == path) return str_dup(".");
    return str_ndup(path, (size_t)(p - path));
}

/* ─── Logging ────────────────────────────────────────────────────────────── */

void wsh_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf)-1] = '\0';
    OutputDebugStringA("[Wsh] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

void wsh_log_win32_error(const char *context) {
    DWORD err = GetLastError();
    char msg[512];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, msg, sizeof(msg), NULL);
    wsh_log("ERROR in %s: [%lu] %s", context, err, msg);
}
