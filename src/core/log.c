/*
 * log.c — Structured logger implementation.
 *
 * Thread-safety: wsh_log_write() acquires a CRITICAL_SECTION so it is safe
 * to call from the PTY reader thread and the UI thread simultaneously.
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "log.h"

/* ── Private state ────────────────────────────────────────────────────────── */

static struct {
    LogLevel          min_level;
    CRITICAL_SECTION  lock;
    HANDLE            h_file;      /* INVALID_HANDLE_VALUE = no file */
    BOOL              initialised; /* Use Win32 BOOL to avoid stdbool dependency */
} g_log;

/* Lazy one-time init using a spinlock on the 'initialised' flag. */
static void log_ensure_init(void) {
    if (!g_log.initialised) {
        InitializeCriticalSection(&g_log.lock);
#ifdef NDEBUG
        g_log.min_level  = LOG_INFO;
#else
        g_log.min_level  = LOG_DEBUG;
#endif
        g_log.h_file     = INVALID_HANDLE_VALUE;
        g_log.initialised = TRUE;
    }
}

void wsh_log_set_level(LogLevel level) {
    log_ensure_init();
    g_log.min_level = level;
}

void wsh_log_set_file(const char *path) {
    log_ensure_init();
    if (!path) return;
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    EnterCriticalSection(&g_log.lock);
    if (g_log.h_file != INVALID_HANDLE_VALUE) CloseHandle(g_log.h_file);
    g_log.h_file = CreateFileW(wpath, GENERIC_WRITE, FILE_SHARE_READ,
                                NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    LeaveCriticalSection(&g_log.lock);
}

static const char *level_tag(LogLevel l) {
    switch (l) {
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        default:        return "???";
    }
}

void wsh_log_write(LogLevel level, const char *file, int line,
                   const char *fmt, ...) {
    log_ensure_init();
    if (level < g_log.min_level) return;

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);

    /* Strip path prefix from __FILE__ for brevity */
    const char *fname = strrchr(file, '\\');
    fname = fname ? fname + 1 : file;

    char line_buf[1152];
    int  n = _snprintf(line_buf, sizeof(line_buf) - 2,
                       "[WSH/%s %s:%d] %s\n", level_tag(level), fname, line, msg);
    if (n < 0) n = (int)sizeof(line_buf) - 2;
    line_buf[n] = '\0';

    /* Wide version for OutputDebugString */
    wchar_t wbuf[1152];
    MultiByteToWideChar(CP_UTF8, 0, line_buf, -1, wbuf, 1152);

    EnterCriticalSection(&g_log.lock);
    OutputDebugStringW(wbuf);
    if (g_log.h_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_log.h_file, line_buf, (DWORD)strlen(line_buf), &written, NULL);
    }
    LeaveCriticalSection(&g_log.lock);
}

void wsh_log_win32(const char *context) {
    DWORD err  = GetLastError();
    char  msg[512] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, msg, sizeof(msg) - 1, NULL);
    char *p = msg + strlen(msg);
    while (p > msg && (p[-1] == '\r' || p[-1] == '\n')) p--;
    *p = '\0';
    WSH_LOG_ERROR("%s: Win32 error %lu - %s", context, (unsigned long)err, msg);
}