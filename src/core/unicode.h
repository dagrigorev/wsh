#pragma once
#ifndef WSH_UNICODE_H
#define WSH_UNICODE_H

#include <windows.h>
#include "wsh_bool.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * unicode.h — zsh-inspired multibyte compatibility layer for WSH.
 *
 * zsh keeps command text as bytes but interprets those bytes through the
 * active multibyte locale when it needs character boundaries or display
 * widths.  WSH uses UTF-8 as its internal byte representation and converts at
 * Win32 boundaries.  These helpers centralise the rules so Cyrillic and other
 * non-ASCII text behaves as characters, not as raw bytes.
 */

void wsh_unicode_init_process(void);

bool wsh_utf8_validate_n(const char *s, int len);
int  wsh_utf8_next_offset(const char *s, int len, int offset);
int  wsh_utf8_prev_offset(const char *s, int offset);
int  wsh_utf8_codepoint_width(uint32_t cp);
int  wsh_utf8_display_width_n(const char *s, int len);
int  wsh_utf8_display_width(const char *s);
int  wsh_utf8_display_width_skip_ansi(const char *s, int len);

char *wsh_bytes_to_utf8_for_terminal(const char *bytes, int len, int *out_len);
char *wsh_utf16_to_utf8_clipboard(const wchar_t *w, int *out_len);
wchar_t *wsh_utf8_to_utf16_clipboard(const char *s, int len, int *out_wchars);

#ifdef __cplusplus
}
#endif

#endif /* WSH_UNICODE_H */
