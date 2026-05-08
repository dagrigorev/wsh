#include "common.hpp"

static void help() {
    std::wcout << L"tree - print a directory tree\n\n"
               << L"Usage: tree [options] [path]\n\n"
               << L"Options:\n"
               << L"  -a, --all       include hidden files\n"
               << L"  -d, --dirs      show directories only\n"
               << L"  -L <n>          maximum depth\n"
               << L"  -h, --help      show this help\n\n"
               << L"Manual: man tree\n";
}

struct Opt { bool all=false; bool dirs=false; int maxDepth=8; };

static void walk(const std::wstring& path, const std::wstring& prefix, int depth, const Opt& opt) {
    if (depth > opt.maxDepth) return;
    std::vector<WIN32_FIND_DATAW> items;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(join_path(path, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        WSH_LOG_WARN("tree: cannot enumerate '%ls'", path.c_str());
        wsh_log_win32("FindFirstFileW");
        return;
    }
    do {
        if (is_dots(fd.cFileName)) continue;
        bool hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!opt.all && hidden) continue;
        if (opt.dirs && !dir) continue;
        items.push_back(fd);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(items.begin(), items.end(), [](const WIN32_FIND_DATAW& a, const WIN32_FIND_DATAW& b) {
        bool ad=(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0, bd=(b.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if (ad != bd) return ad > bd;
        return _wcsicmp(a.cFileName, b.cFileName) < 0;
    });
    for (size_t i=0; i<items.size(); ++i) {
        bool last = i + 1 == items.size();
        bool dir = (items[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wcout << prefix << (last ? L"└── " : L"├── ") << items[i].cFileName << (dir ? L"/" : L"") << L"\n";
        if (dir) walk(join_path(path, items[i].cFileName), prefix + (last ? L"    " : L"│   "), depth + 1, opt);
    }
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    Opt opt; std::wstring path = L".";
    for (int i=1; i<argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-a" || a == L"--all") opt.all = true;
        else if (a == L"-d" || a == L"--dirs") opt.dirs = true;
        else if (a == L"-L" && i + 1 < argc) opt.maxDepth = _wtoi(argv[++i]);
        else path = a;
    }
    std::wcout << path << L"\n";
    walk(path, L"", 1, opt);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("tree", [&]() { return tool_main(argc, argv); });
}
