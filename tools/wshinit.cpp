#include "common.hpp"
#include <fstream>

static void help() {
    std::wcout << L"wshinit - initialize Wsh user environment\n\n"
               << L"Usage: wshinit [options]\n\n"
               << L"Options:\n"
               << L"  --force        overwrite existing Wsh.toml\n"
               << L"  --restart      start Wsh.exe after initialization\n"
               << L"  -h, --help     show this help\n\n"
               << L"Creates %APPDATA%\\Wsh, Wsh.toml, themes and man directories.\n"
               << L"Manual: man wshinit\n";
}

static std::wstring appdata_wsh() {
    wchar_t buf[MAX_PATH]{};
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    return join_path(buf, L"Wsh");
}

static std::wstring exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

static void copy_dir_files(const std::wstring& src, const std::wstring& dst) {
    CreateDirectoryW(dst.c_str(), nullptr);
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(join_path(src, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (is_dots(fd.cFileName) || (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        CopyFileW(join_path(src, fd.cFileName).c_str(), join_path(dst, fd.cFileName).c_str(), FALSE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static bool write_default_config(const std::wstring& path, bool force) {
    if (!force && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    std::wofstream f(path.c_str());
    if (!f) return false;
    f << L"[general]\n"
      << L"shell = \"wsh\"\n"
      << L"scrollback = 10000\n"
      << L"confirm_exit = true\n"
      << L"bell = \"visual\"\n"
      << L"default_cwd = \"~\"\n"
      << L"theme = \"catppuccin-mocha\"\n"
      << L"title = \"Wsh - ${cwd}\"\n\n"
      << L"[font]\nfamily = \"Cascadia Code\"\nsize = 13.0\nligatures = true\nbold_is_bright = true\n\n"
      << L"[cursor]\nstyle = \"block\"\nblink = true\nblink_rate_ms = 530\n";
    return true;
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    bool force=false, restart=false;
    for (int i=1; i<argc; ++i) {
        std::wstring a=argv[i];
        if (a==L"--force") force=true;
        else if (a==L"--restart") restart=true;
    }
    std::wstring root = appdata_wsh();
    CreateDirectoryW(root.c_str(), nullptr);
    bool ok = write_default_config(join_path(root, L"Wsh.toml"), force);
    std::wstring ed = exe_dir();
    copy_dir_files(join_path(ed, L"themes"), join_path(root, L"themes"));
    copy_dir_files(join_path(ed, L"man"), join_path(root, L"man"));
    std::wcout << L"Wsh environment initialized at " << root << L"\n";
    if (!ok) {
        std::wcerr << L"wshinit: failed to write config\n";
        WSH_LOG_ERROR("wshinit: failed to write config at '%ls'", join_path(root, L"Wsh.toml").c_str());
        return 1;
    }
    if (restart) {
        std::wstring exe = join_path(ed, L"Wsh.exe");
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, ed.c_str(), SW_SHOWNORMAL);
    }
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("wshinit", [&]() { return tool_main(argc, argv); });
}
