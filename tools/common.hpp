#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <exception>
#include <sstream>
#include "log.h"

inline std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &out[0], needed, nullptr, nullptr);
    return out;
}

inline std::string wide_to_utf8(const wchar_t *value) {
    return value ? wide_to_utf8(std::wstring(value)) : std::string();
}

inline std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &out[0], needed);
    return out;
}

inline bool ansi_enabled(bool requested = true) {
    if (!requested) return false;
    if (GetEnvironmentVariableW(L"NO_COLOR", nullptr, 0) > 0) return false;
    if (GetEnvironmentVariableW(L"WSH_TERM", nullptr, 0) > 0) return true;
    if (GetEnvironmentVariableW(L"COLORTERM", nullptr, 0) > 0) return true;
    wchar_t term[64] = {0};
    if (GetEnvironmentVariableW(L"TERM", term, 64) > 0) {
        if (wcsstr(term, L"xterm") || wcsstr(term, L"ansi") || wcsstr(term, L"vt100") || wcsstr(term, L"256color")) return true;
    }
    DWORD mode = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) return false;
    if (GetFileType(out) != FILE_TYPE_CHAR) return false;
    if (GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return true;
}

inline const char *ansi_reset(bool color) { return color ? "\x1b[0m" : ""; }
inline const char *ansi_dim(bool color) { return color ? "\x1b[2m" : ""; }
inline const char *ansi_bold(bool color) { return color ? "\x1b[1m" : ""; }
inline const char *ansi_blue(bool color) { return color ? "\x1b[38;2;79;142;247m" : ""; }
inline const char *ansi_cyan(bool color) { return color ? "\x1b[38;2;79;207;247m" : ""; }
inline const char *ansi_green(bool color) { return color ? "\x1b[38;2;61;214;140m" : ""; }
inline const char *ansi_amber(bool color) { return color ? "\x1b[38;2;247;184;79m" : ""; }
inline const char *ansi_red(bool color) { return color ? "\x1b[38;2;247;96;79m" : ""; }
inline const char *ansi_purple(bool color) { return color ? "\x1b[38;2;169;127;247m" : ""; }

inline bool is_help(int argc, wchar_t **argv) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--help" || a == L"-h" || a == L"/?") return true;
    }
    return false;
}

inline std::wstring current_dir() {
    DWORD n = GetCurrentDirectoryW(0, nullptr);
    std::wstring s(n ? n - 1 : 0, L'\0');
    if (n) GetCurrentDirectoryW(n, &s[0]);
    return s;
}

inline std::wstring join_path(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

inline bool is_dots(const wchar_t *name) {
    return wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
}

inline std::wstring quote_arg(const std::wstring& arg) {
    std::wstring out = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'"') out += L"\\\"";
        else out += ch;
    }
    out += L"\"";
    return out;
}

inline std::string run_capture_utf8(const std::wstring& command) {
    FILE *pipe = _wpopen(command.c_str(), L"rb");
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), pipe);
        if (n > 0) out.append(buf, n);
        if (n < sizeof(buf)) {
            if (feof(pipe)) break;
            if (ferror(pipe)) break;
        }
    }
    _pclose(pipe);
    return out;
}

inline std::wstring normalize_slashes(std::wstring s) {
    for (wchar_t& ch : s) if (ch == L'/') ch = L'\\';
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

template <typename Fn>
inline int run_tool_logged(const char *tool_name, Fn fn) {
    wsh_log_init_default(tool_name);
    wsh_log_set_max_file_size(10ULL * 1024ULL * 1024ULL);
    wsh_log_install_crash_handlers();
    WSH_LOG_INFO("%s starting", tool_name ? tool_name : "tool");
    try {
        int rc = fn();
        WSH_LOG_INFO("%s exited with code %d", tool_name ? tool_name : "tool", rc);
        wsh_log_close();
        return rc;
    } catch (const std::exception& ex) {
        WSH_LOG_ERROR("Unhandled C++ exception in %s: %s", tool_name ? tool_name : "tool", ex.what());
    } catch (...) {
        WSH_LOG_ERROR("Unhandled unknown C++ exception in %s", tool_name ? tool_name : "tool");
    }
    wsh_log_close();
    return 1;
}
