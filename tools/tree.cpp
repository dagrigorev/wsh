#include "common.hpp"
#include <map>
#include <set>

static void help() {
    std::cout << "tree - print a directory tree\n\n"
              << "Usage: tree [options] [path]\n\n"
              << "Options:\n"
              << "  -a, --all       include hidden files\n"
              << "  -d, --dirs      show directories only\n"
              << "  -L <n>          maximum depth\n"
              << "  --git           show real Git tracked/dirty markers (default in repos)\n"
              << "  --no-git        hide Git markers\n"
              << "  --color=WHEN    color output: auto, always, never\n"
              << "  -h, --help      show this help\n\n"
              << "Manual: man tree\n";
}

struct Opt { bool all=false; bool dirs=false; int maxDepth=8; bool git=true; bool color=true; bool force_color=false; };

struct GitState {
    std::wstring root;
    std::set<std::wstring> tracked;
    std::map<std::wstring, wchar_t> dirty;
    bool available = false;
};

static std::wstring absolute_path(const std::wstring& path) {
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed) return path;
    std::wstring out((size_t)needed + 1, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), needed + 1, &out[0], nullptr);
    out.resize(written);
    return normalize_slashes(out);
}

static std::wstring rel_to_git(const std::wstring& full, const GitState& git) {
    std::wstring f = normalize_slashes(absolute_path(full));
    std::wstring r = normalize_slashes(git.root);
    if (f.size() < r.size() || _wcsnicmp(f.c_str(), r.c_str(), r.size()) != 0) return {};
    size_t off = r.size();
    if (off < f.size() && (f[off] == L'\\' || f[off] == L'/')) off++;
    std::wstring rel = f.substr(off);
    for (wchar_t& ch : rel) if (ch == L'\\') ch = L'/';
    return rel;
}

static bool path_exists_w(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring parent_dir(std::wstring path) {
    path = normalize_slashes(path);
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return {};
    if (p <= 2 && path.size() > 1 && path[1] == L':') return {};
    return path.substr(0, p);
}

static std::wstring find_git_root(std::wstring path) {
    path = absolute_path(path);
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        path = parent_dir(path);
    }
    while (!path.empty()) {
        if (path_exists_w(join_path(path, L".git"))) return normalize_slashes(path);
        std::wstring parent = parent_dir(path);
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return {};
}

static GitState load_git_state(const std::wstring& path) {
    GitState git;
    git.root = find_git_root(path);
    if (git.root.empty()) return git;
    git.available = true;

    std::string files = run_capture_utf8(L"git -C " + quote_arg(git.root) + L" ls-files -z 2>NUL");
    size_t start = 0;
    while (start < files.size()) {
        size_t end = files.find('\0', start);
        if (end == std::string::npos) break;
        if (end > start) git.tracked.insert(utf8_to_wide(files.substr(start, end - start)));
        start = end + 1;
    }

    std::string status = run_capture_utf8(L"git -C " + quote_arg(git.root) + L" status --porcelain=v1 -z --untracked-files=all 2>NUL");
    start = 0;
    while (start + 3 <= status.size()) {
        char x = status[start], y = status[start + 1];
        size_t path_start = start + 3;
        size_t end = status.find('\0', path_start);
        if (end == std::string::npos) break;
        std::wstring rel = utf8_to_wide(status.substr(path_start, end - path_start));
        wchar_t mark = L'M';
        if (x == '?' && y == '?') mark = L'?';
        else if (x == 'A' || y == 'A') mark = L'A';
        else if (x == 'D' || y == 'D') mark = L'D';
        else if (x == 'R' || y == 'R') mark = L'R';
        else if (x == 'U' || y == 'U') mark = L'U';
        git.dirty[rel] = mark;
        start = end + 1;
        if ((x == 'R' || x == 'C') && start < status.size()) {
            size_t renamed_end = status.find('\0', start);
            if (renamed_end == std::string::npos) break;
            start = renamed_end + 1;
        }
    }
    return git;
}

static wchar_t git_mark_for(const std::wstring& full, bool dir, const GitState& git) {
    if (!git.available) return 0;
    std::wstring rel = rel_to_git(full, git);
    if (rel.empty()) return 0;
    auto dirty = git.dirty.find(rel);
    if (dirty != git.dirty.end()) return dirty->second;
    if (dir) {
        std::wstring prefix = rel + L"/";
        for (const auto& kv : git.dirty) {
            if (kv.first.rfind(prefix, 0) == 0) return kv.second;
        }
        for (const auto& tracked : git.tracked) {
            if (tracked.rfind(prefix, 0) == 0) return L'C';
        }
        return 0;
    }
    return git.tracked.count(rel) ? L'C' : 0;
}

static const char *git_mark_color(wchar_t mark, bool color) {
    switch (mark) {
        case L'C': return ansi_green(color);
        case L'?': return ansi_red(color);
        case L'A': return ansi_green(color);
        case L'D': return ansi_red(color);
        case L'U': return ansi_purple(color);
        case L'R': return ansi_cyan(color);
        case L'M': default: return ansi_amber(color);
    }
}

static std::string git_mark_text(wchar_t mark, bool color) {
    if (!mark) return "";
    std::string s;
    s += git_mark_color(mark, color);
    s += ansi_bold(color);
    s += "[";
    s += (char)mark;
    s += "]";
    s += ansi_reset(color);
    s += " ";
    return s;
}

static void walk(const std::wstring& path, const std::string& prefix, int depth, const Opt& opt, const GitState& git) {
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
        std::wstring full = join_path(path, items[i].cFileName);
        wchar_t mark = opt.git ? git_mark_for(full, dir, git) : 0;
        const char *name_color = dir ? ansi_blue(opt.color) : ansi_reset(opt.color);
        if (!dir && mark == L'?') name_color = ansi_red(opt.color);
        else if (!dir && mark && mark != L'C') name_color = ansi_amber(opt.color);
        std::cout << prefix << (last ? "`-- " : "|-- ")
                  << git_mark_text(mark, opt.color)
                  << name_color << wide_to_utf8(items[i].cFileName) << (dir ? "/" : "")
                  << ansi_reset(opt.color) << "\n";
        if (dir) walk(full, prefix + (last ? "    " : "|   "), depth + 1, opt, git);
    }
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    Opt opt; std::wstring path = L".";
    for (int i=1; i<argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-a" || a == L"--all") opt.all = true;
        else if (a == L"-d" || a == L"--dirs") opt.dirs = true;
        else if (a == L"--git") opt.git = true;
        else if (a == L"--no-git") opt.git = false;
        else if (a == L"--color=always") { opt.color = true; opt.force_color = true; }
        else if (a == L"--color=never") { opt.color = false; opt.force_color = false; }
        else if (a == L"--color=auto") { opt.color = ansi_enabled(true); opt.force_color = false; }
        else if (a == L"-L" && i + 1 < argc) opt.maxDepth = _wtoi(argv[++i]);
        else path = a;
    }
    if (opt.color && !opt.force_color) opt.color = ansi_enabled(true);
    GitState git = opt.git ? load_git_state(path) : GitState{};
    std::cout << ansi_bold(opt.color) << ansi_cyan(opt.color) << wide_to_utf8(path)
              << ansi_reset(opt.color);
    if (git.available && opt.git) {
        std::cout << " " << ansi_dim(opt.color) << "(git: "
                  << wide_to_utf8(rel_to_git(path, git).empty() ? git.root : git.root)
                  << ")" << ansi_reset(opt.color);
    }
    std::cout << "\n";
    walk(path, "", 1, opt, git);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("tree", [&]() { return tool_main(argc, argv); });
}
