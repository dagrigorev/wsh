#include "common.hpp"

static void help() {
    std::wcout << L"ls - list directory contents\n\n"
               << L"Usage: ls [options] [path]\n\n"
               << L"Options:\n"
               << L"  -a, --all       include hidden files\n"
               << L"  -l, --long      print attributes, size and name\n"
               << L"  -h, --help      show this help\n\n"
               << L"Manual: man ls\n";
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
    bool all = false, lng = false;
    std::wstring path = L".";
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-a" || a == L"--all") all = true;
        else if (a == L"-l" || a == L"--long") lng = true;
        else path = a;
    }
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
        if (lng) {
            ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            std::wcout << attr_string(fd.dwFileAttributes) << L"\t" << size << L"\t" << fd.cFileName << L"\n";
        } else {
            std::wcout << fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) std::wcout << L"/";
            std::wcout << L"\n";
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("ls", [&]() { return tool_main(argc, argv); });
}
