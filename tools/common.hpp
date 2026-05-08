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
#include "log.h"

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
