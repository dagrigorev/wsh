#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iomanip>
#include <map>
#include "common.hpp"

struct ProcSample {
    DWORD pid = 0;
    DWORD ppid = 0;
    std::wstring name;
    ULONGLONG cpu_time = 0;
    SIZE_T working_set = 0;
    DWORD thread_count = 0;
    bool accessible = false;
};

struct ProcRow {
    ProcSample sample;
    double cpu = 0.0;
};

struct Opt {
    int delay_ms = 700;
    int limit = 25;
    bool watch = false;
    bool color = true;
    bool force_color = false;
    std::string sort = "cpu";
};

static ULONGLONG filetime_to_ull(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

static void help() {
    std::cout << "htop - show live Windows process and resource usage\n\n"
              << "Usage: htop [options]\n\n"
              << "Options:\n"
              << "  -n <count>        show at most count processes (default 25)\n"
              << "  -d <ms>           sampling delay in milliseconds (default 700)\n"
              << "  -s <key>          sort by cpu, mem, pid, name (default cpu)\n"
              << "  -w, --watch       refresh until interrupted\n"
              << "  --color=WHEN      color output: auto, always, never\n"
              << "  -h, --help        show this help\n\n"
              << "Manual: man htop\n";
}

static bool parse_args(int argc, wchar_t **argv, Opt *opt) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-n" && i + 1 < argc) opt->limit = _wtoi(argv[++i]);
        else if (a == L"-d" && i + 1 < argc) opt->delay_ms = _wtoi(argv[++i]);
        else if (a == L"-s" && i + 1 < argc) opt->sort = wide_to_utf8(argv[++i]);
        else if (a == L"-w" || a == L"--watch") opt->watch = true;
        else if (a == L"--color=always") { opt->color = true; opt->force_color = true; }
        else if (a == L"--color=never") { opt->color = false; opt->force_color = false; }
        else if (a == L"--color=auto") { opt->color = ansi_enabled(true); opt->force_color = false; }
        else return false;
    }
    if (opt->limit < 1) opt->limit = 1;
    if (opt->limit > 200) opt->limit = 200;
    if (opt->delay_ms < 100) opt->delay_ms = 100;
    if (opt->delay_ms > 10000) opt->delay_ms = 10000;
    if (opt->sort != "cpu" && opt->sort != "mem" && opt->sort != "pid" && opt->sort != "name") opt->sort = "cpu";
    if (opt->color && !opt->force_color) opt->color = ansi_enabled(true);
    return true;
}

static int cpu_count() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

static std::map<DWORD, ProcSample> collect_processes() {
    std::map<DWORD, ProcSample> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        CloseHandle(snap);
        return out;
    }

    do {
        ProcSample ps;
        ps.pid = pe.th32ProcessID;
        ps.ppid = pe.th32ParentProcessID;
        ps.name = pe.szExeFile;
        ps.thread_count = pe.cntThreads;

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, ps.pid);
        if (h) {
            FILETIME create{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(h, &create, &exit, &kernel, &user)) {
                ps.cpu_time = filetime_to_ull(kernel) + filetime_to_ull(user);
                ps.accessible = true;
            }
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                ps.working_set = pmc.WorkingSetSize;
            }
            CloseHandle(h);
        }
        out[ps.pid] = ps;
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return out;
}

static double system_cpu_percent(const FILETIME& idle0, const FILETIME& kernel0, const FILETIME& user0,
                                 const FILETIME& idle1, const FILETIME& kernel1, const FILETIME& user1) {
    ULONGLONG idle = filetime_to_ull(idle1) - filetime_to_ull(idle0);
    ULONGLONG kernel = filetime_to_ull(kernel1) - filetime_to_ull(kernel0);
    ULONGLONG user = filetime_to_ull(user1) - filetime_to_ull(user0);
    ULONGLONG total = kernel + user;
    if (total == 0) return 0.0;
    return ((double)(total - idle) * 100.0) / (double)total;
}

static std::string human_bytes(ULONGLONG bytes) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[32];
    if (u == 0) _snprintf(buf, sizeof(buf), "%.0f%s", v, units[u]);
    else _snprintf(buf, sizeof(buf), "%.1f%s", v, units[u]);
    return buf;
}

static void bar(double pct, int width, bool color) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    int filled = (int)((pct / 100.0) * width + 0.5);
    const char *c = pct > 85.0 ? ansi_red(color) : (pct > 65.0 ? ansi_amber(color) : ansi_green(color));
    std::cout << ansi_dim(color) << "[" << ansi_reset(color) << c;
    for (int i = 0; i < width; ++i) std::cout << (i < filled ? "#" : "-");
    std::cout << ansi_reset(color) << ansi_dim(color) << "]" << ansi_reset(color);
}

static void render_once(const Opt& opt) {
    FILETIME idle0{}, kernel0{}, user0{}, idle1{}, kernel1{}, user1{};
    GetSystemTimes(&idle0, &kernel0, &user0);
    auto p0 = collect_processes();
    Sleep((DWORD)opt.delay_ms);
    GetSystemTimes(&idle1, &kernel1, &user1);
    auto p1 = collect_processes();

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    double cpu = system_cpu_percent(idle0, kernel0, user0, idle1, kernel1, user1);
    double mem_pct = mem.ullTotalPhys ? ((double)(mem.ullTotalPhys - mem.ullAvailPhys) * 100.0 / (double)mem.ullTotalPhys) : 0.0;

    std::vector<ProcRow> rows;
    rows.reserve(p1.size());
    double elapsed_100ns = (double)(opt.delay_ms) * 10000.0;
    int cpus = cpu_count();
    for (const auto& kv : p1) {
        ProcRow row;
        row.sample = kv.second;
        auto prev = p0.find(kv.first);
        if (prev != p0.end() && elapsed_100ns > 0.0) {
            ULONGLONG delta = row.sample.cpu_time >= prev->second.cpu_time ? row.sample.cpu_time - prev->second.cpu_time : 0;
            row.cpu = ((double)delta * 100.0) / (elapsed_100ns * (double)cpus);
        }
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [&](const ProcRow& a, const ProcRow& b) {
        if (opt.sort == "mem") return a.sample.working_set > b.sample.working_set;
        if (opt.sort == "pid") return a.sample.pid < b.sample.pid;
        if (opt.sort == "name") return _wcsicmp(a.sample.name.c_str(), b.sample.name.c_str()) < 0;
        return a.cpu > b.cpu;
    });

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::cout << ansi_bold(opt.color) << ansi_cyan(opt.color) << "WSH htop"
              << ansi_reset(opt.color) << "  "
              << std::setfill('0') << std::setw(2) << st.wHour << ":"
              << std::setw(2) << st.wMinute << ":" << std::setw(2) << st.wSecond
              << std::setfill(' ') << "\n";

    std::cout << "CPU ";
    bar(cpu, 28, opt.color);
    std::cout << " " << std::fixed << std::setprecision(1) << cpu << "%\n";
    std::cout << "RAM ";
    bar(mem_pct, 28, opt.color);
    std::cout << " " << human_bytes(mem.ullTotalPhys - mem.ullAvailPhys) << "/"
              << human_bytes(mem.ullTotalPhys) << " " << std::fixed << std::setprecision(1) << mem_pct << "%\n\n";

    std::cout << ansi_dim(opt.color)
              << "PID      PPID     CPU%   MEM       THR  NAME"
              << ansi_reset(opt.color) << "\n";
    int shown = 0;
    for (const ProcRow& row : rows) {
        if (shown++ >= opt.limit) break;
        const char *cpu_col = row.cpu > 30.0 ? ansi_red(opt.color) : (row.cpu > 10.0 ? ansi_amber(opt.color) : ansi_green(opt.color));
        std::cout << std::setw(8) << row.sample.pid
                  << " " << std::setw(8) << row.sample.ppid
                  << " " << cpu_col << std::setw(6) << std::fixed << std::setprecision(1) << row.cpu << ansi_reset(opt.color)
                  << " " << ansi_cyan(opt.color) << std::setw(9) << human_bytes((ULONGLONG)row.sample.working_set) << ansi_reset(opt.color)
                  << " " << std::setw(5) << row.sample.thread_count
                  << "  " << (row.sample.accessible ? "" : ansi_dim(opt.color))
                  << wide_to_utf8(row.sample.name) << ansi_reset(opt.color) << "\n";
    }
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    Opt opt;
    if (!parse_args(argc, argv, &opt)) {
        help();
        return 1;
    }
    do {
        if (opt.watch) std::cout << "\x1b[2J\x1b[H";
        render_once(opt);
        if (!opt.watch) break;
    } while (true);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("htop", [&]() { return tool_main(argc, argv); });
}
