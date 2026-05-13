#include "common.hpp"

static void help() {
    std::wcout << L"md - create directories\n\n"
               << L"Usage: md <directory> [directory...]\n\n"
               << L"Creates each directory including missing parent folders.\n"
               << L"Manual: man md\n";
}

static bool mkdirs(const std::wstring& path) {
    if (path.empty()) return false;
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t ch = path[i];
        cur.push_back(ch);
        if ((ch == L'\\' || ch == L'/') && cur.size() > 1) CreateDirectoryW(cur.c_str(), nullptr);
    }
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static int tool_main(int argc, wchar_t **argv) {
    if (argc < 2 || is_help(argc, argv)) { help(); return argc < 2 ? 1 : 0; }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (!mkdirs(argv[i])) {
            std::wcerr << L"md: cannot create '" << argv[i] << L"'\n";
            WSH_LOG_ERROR("md: cannot create '%ls'", argv[i]);
            wsh_log_win32("CreateDirectoryW");
            rc = 1;
        }
    }
    return rc;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("md", [&]() { return tool_main(argc, argv); });
}
