#include "common.hpp"

static void help() {
    std::cout << "ls - list directory contents\n\n"
              << "Usage: ls [options] [path]\n\n"
              << "Options:\n"
              << "  -a, --all       include hidden files\n"
              << "  -l, --long      print attributes, size and name\n"
              << "  --color=WHEN    color output: auto, always, never\n"
              << "  -h, --help      show this help\n\n"
              << "Manual: man ls\n";
}

static std::wstring attr_string(DWORD a) {
    std::wstring s;
    s += (a & FILE_ATTRIBUTE_DIRECTORY) ? L'd' : L'-';
    s += (a & FILE_ATTRIBUTE_READONLY)  ? L'r' : L'w';
    s += (a & FILE_ATTRIBUTE_HIDDEN)    ? L'h' : L'-';
    s += (a & FILE_ATTRIBUTE_SYSTEM)    ? L's' : L'-';
    s += (a & FILE_ATTRIBUTE_ARCHIVE)   ? L'a' : L'-';
    return s;
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    bool all = false, lng = false, color = ansi_enabled(true), force_color = false;
    std::wstring path = L".";
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--all") all = true;
        else if (a == L"--long") lng = true;
        else if (a == L"--color=always") { color = true; force_color = true; }
        else if (a == L"--color=never") { color = false; force_color = false; }
        else if (a == L"--color=auto") { color = ansi_enabled(true); force_color = false; }
        else if (a.size() >= 2 && a[0] == L'-' && a[1] != L'-') {
            for (size_t j = 1; j < a.size(); ++j) {
                if (a[j] == L'a') all = true;
                else if (a[j] == L'l') lng = true;
            }
        } else if (a[0] != L'-') {
            path = a;
        }
    }
    if (color && !force_color) color = ansi_enabled(true);
    std::wstring mask = join_path(path, L"*");
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        std::wcerr << L"ls: cannot access '" << path << L"'\n";
        WSH_LOG_ERROR("ls: cannot access '%ls'", path.c_str());
        wsh_log_win32("FindFirstFileW");
        return 2;
    }
    do {
        if (!all && (is_dots(fd.cFileName) || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))) continue;
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        std::wstring namew = fd.cFileName;
        const wchar_t *dot = wcsrchr(fd.cFileName, L'.');
        bool executable = dot && (!_wcsicmp(dot, L".exe") || !_wcsicmp(dot, L".bat") || !_wcsicmp(dot, L".cmd") || !_wcsicmp(dot, L".ps1"));
        const char *name_color = is_dir ? ansi_blue(color) : (executable ? ansi_green(color) : (hidden ? ansi_dim(color) : ansi_reset(color)));
        if (lng) {
            ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            std::cout << ansi_dim(color) << wide_to_utf8(attr_string(fd.dwFileAttributes)) << ansi_reset(color)
                      << "\t" << ansi_cyan(color) << size << ansi_reset(color) << "\t"
                      << name_color << wide_to_utf8(namew) << (is_dir ? "/" : "") << ansi_reset(color) << "\n";
        } else {
            std::cout << name_color << wide_to_utf8(namew);
            if (is_dir) std::cout << "/";
            std::cout << ansi_reset(color) << "\n";
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("ls", [&]() { return tool_main(argc, argv); });
}
