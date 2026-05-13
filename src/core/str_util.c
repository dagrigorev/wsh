/*
 * str_util.c — String and UTF conversion implementations.
 *
 * Key decisions:
 *   - All heap strings use the Win32 process heap (HeapAlloc/HeapFree) so
 *     that allocations are visible in WinDbg and can be tracked with heap
 *     leak detection enabled.
 *   - UTF-8 / UTF-16 conversion goes through Win32 WideCharToMultiByte /
 *     MultiByteToWideChar which correctly handles all of Unicode including
 *     surrogate pairs (emoji, CJK extensions).
 *   - str_split returns a NULL-sentinel array so callers can iterate without
 *     needing the count separately.
 */
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "str_util.h"

/* ── Heap helpers ─────────────────────────────────────────────────────────── */

char *str_dup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char  *p = (char *)HeapAlloc(GetProcessHeap(), 0, n);
    if (p) memcpy(p, s, n);
    return p;
}

char *str_ndup(const char *s, size_t n) {
    if (!s) return str_dup("");
    char *p = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

char *str_join(char **parts, int count, const char *sep) {
    if (count <= 0) return str_dup("");
    size_t sep_len = sep ? strlen(sep) : 0;
    size_t total   = 0;
    for (int i = 0; i < count; i++) total += parts[i] ? strlen(parts[i]) : 0;
    total += sep_len * (size_t)(count - 1) + 1;

    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, total);
    if (!out) return NULL;
    char *p = out;
    for (int i = 0; i < count; i++) {
        if (i > 0 && sep) { memcpy(p, sep, sep_len); p += sep_len; }
        if (parts[i]) { size_t l = strlen(parts[i]); memcpy(p, parts[i], l); p += l; }
    }
    *p = '\0';
    return out;
}

char *str_rtrim(char *s) {
    if (!s) return s;
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) p--;
    *p = '\0';
    return s;
}

char *str_trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    str_rtrim(s);
    return s;
}

bool str_startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    size_t pl = strlen(prefix);
    return strncmp(s, prefix, pl) == 0;
}

int str_split(const char *src, char delim, char ***parts) {
    *parts = NULL;
    if (!src) return 0;

    /* Count tokens */
    int count = 1;
    for (const char *p = src; *p; p++) if (*p == delim) count++;

    *parts = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 (size_t)(count + 1) * sizeof(char *));
    if (!*parts) return 0;

    int idx  = 0;
    const char *start = src;
    for (const char *p = src; ; p++) {
        if (*p == delim || *p == '\0') {
            (*parts)[idx++] = str_ndup(start, (size_t)(p - start));
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    (*parts)[idx] = NULL; /* sentinel */
    return idx;
}

void str_split_free(char **parts, int count) {
    if (!parts) return;
    for (int i = 0; i < count; i++) str_free(parts[i]);
    HeapFree(GetProcessHeap(), 0, parts);
}

/* ── UTF conversion ───────────────────────────────────────────────────────── */

wchar_t *u8_to_u16(const char *utf8, int *out_wlen) {
    if (!utf8) { if (out_wlen) *out_wlen = 0; return NULL; }
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    if (out_wlen) *out_wlen = n - 1; /* exclude NUL */
    return w;
}

char *u16_to_u8(const wchar_t *utf16, int *out_len) {
    if (!utf16) { if (out_len) *out_len = 0; return NULL; }
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, utf16, -1, s, n, NULL, NULL);
    if (out_len) *out_len = n - 1;
    return s;
}

/* RFC-3629 UTF-8 decoder — handles 1-4 byte sequences. */
unsigned int utf8_decode(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned int cp;
    int extra;

    if (*s < 0x80)        { cp = *s;            extra = 0; }
    else if (*s < 0xC0)   { cp = 0xFFFD;        extra = 0; } /* continuation byte — error */
    else if (*s < 0xE0)   { cp = *s & 0x1F;     extra = 1; }
    else if (*s < 0xF0)   { cp = *s & 0x0F;     extra = 2; }
    else if (*s < 0xF8)   { cp = *s & 0x07;     extra = 3; }
    else                  { cp = 0xFFFD;        extra = 0; }

    s++;
    for (int i = 0; i < extra; i++) {
        if ((*s & 0xC0) != 0x80) { cp = 0xFFFD; break; }
        cp = (cp << 6) | (*s++ & 0x3F);
    }
    *p = (const char *)s;
    return cp;
}

int utf8_encode(unsigned int cp, char *buf) {
    if (cp < 0x80)    { buf[0] = (char)cp;                               return 1; }
    if (cp < 0x800)   { buf[0] = (char)(0xC0|(cp>>6));
                        buf[1] = (char)(0x80|(cp&0x3F));                  return 2; }
    if (cp < 0x10000) { buf[0] = (char)(0xE0|(cp>>12));
                        buf[1] = (char)(0x80|((cp>>6)&0x3F));
                        buf[2] = (char)(0x80|(cp&0x3F));                  return 3; }
                      { buf[0] = (char)(0xF0|(cp>>18));
                        buf[1] = (char)(0x80|((cp>>12)&0x3F));
                        buf[2] = (char)(0x80|((cp>>6)&0x3F));
                        buf[3] = (char)(0x80|(cp&0x3F));                  return 4; }
}
