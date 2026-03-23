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
#include <stdbool.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

/* Set minimum level (default: INFO in Release, DEBUG in Debug). */
void wsh_log_set_level(LogLevel level);

/* Optionally append log output to a file (UTF-8). */
void wsh_log_set_file(const char *path);

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

#endif /* WSH_LOG_H */
