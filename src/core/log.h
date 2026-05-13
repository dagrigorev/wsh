#pragma once
/*
 * log.h — Minimal structured logger.
 *
 * Levels: DEBUG, INFO, WARN, ERROR.
 * In Release builds, DEBUG is compiled out entirely (zero overhead).
 * Output goes to OutputDebugStringW (viewable in DebugView / WinDbg)
 * and optionally to a log file set via wsh_log_set_file().
 *
 * Design pattern: Singleton (one global log state, initialised lazily).
 * OOP: Encapsulation — log state hidden behind functional API.
 */
#ifndef WSH_LOG_H
#define WSH_LOG_H

#include <windows.h>
#include "wsh_bool.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

/* Set minimum level (default: INFO in Release, DEBUG in Debug). */
void wsh_log_set_level(LogLevel level);

/* Initialize default file logging under %LOCALAPPDATA%\Wsh\logs.
 * Fallback: <exe-dir>\logs. The file is append-only until it reaches
 * the configured size limit, then it is truncated and reused.
 */
void wsh_log_init_default(const char *app_name);

/* Install process-level crash handlers that log unhandled Win32/CRT faults. */
void wsh_log_install_crash_handlers(void);

/* Optionally append log output to a file (UTF-8). */
void wsh_log_set_file(const char *path);

/* Set max log file size in bytes. Default: 10 MiB. */
void wsh_log_set_max_file_size(unsigned long long max_bytes);

/* Flush and close the active log file. */
void wsh_log_close(void);

/* Core log function — prefer the macros below. */
void wsh_log_write(LogLevel level, const char *file, int line, const char *fmt, ...);

/* Log the last Win32 error with a message prefix. */
void wsh_log_win32(const char *context);

/* Convenience macros — DEBUG is a no-op in Release. */
#ifdef NDEBUG
#  define WSH_LOG_DEBUG(fmt, ...) ((void)0)
#else
#  define WSH_LOG_DEBUG(fmt, ...) wsh_log_write(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#endif

#define WSH_LOG_INFO(fmt,  ...) wsh_log_write(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define WSH_LOG_WARN(fmt,  ...) wsh_log_write(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define WSH_LOG_ERROR(fmt, ...) wsh_log_write(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif

#endif /* WSH_LOG_H */
