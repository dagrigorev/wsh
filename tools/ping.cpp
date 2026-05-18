#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <chrono>
#include "common.hpp"

struct PingOpt {
    int count = 4;
    int timeout_ms = 1000;
    int interval_ms = 1000;
    int size = 32;
    bool continuous = false;
    bool color = true;
    bool force_color = false;
};

static void help() {
    std::cout << "ping - send ICMP echo probes\n\n"
              << "Usage: ping [options] host\n\n"
              << "Options:\n"
              << "  -c, -n <count>      number of probes (default 4)\n"
              << "  -t, --continuous    ping until interrupted\n"
              << "  -w <ms>             reply timeout in milliseconds\n"
              << "  -i <ms>             interval between probes in milliseconds\n"
              << "  -s <bytes>          payload size, 1..1400\n"
              << "  --color=WHEN        color output: auto, always, never\n"
              << "  -h, --help          show this help\n\n"
              << "Manual: man ping\n";
}

static bool parse_int_arg(int argc, wchar_t **argv, int *i, int *out) {
    if (*i + 1 >= argc) return false;
    *out = _wtoi(argv[++(*i)]);
    return true;
}

static bool parse_args(int argc, wchar_t **argv, PingOpt *opt, std::wstring *host) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-c" || a == L"-n") {
            if (!parse_int_arg(argc, argv, &i, &opt->count)) return false;
        } else if (a == L"-t" || a == L"--continuous") {
            opt->continuous = true;
        } else if (a == L"-w") {
            if (!parse_int_arg(argc, argv, &i, &opt->timeout_ms)) return false;
        } else if (a == L"-i") {
            if (!parse_int_arg(argc, argv, &i, &opt->interval_ms)) return false;
        } else if (a == L"-s") {
            if (!parse_int_arg(argc, argv, &i, &opt->size)) return false;
        } else if (a == L"--color=always") {
            opt->color = true;
            opt->force_color = true;
        } else if (a == L"--color=never") {
            opt->color = false;
            opt->force_color = false;
        } else if (a == L"--color=auto") {
            opt->color = ansi_enabled(true);
            opt->force_color = false;
        } else if (!a.empty() && a[0] == L'-') {
            return false;
        } else {
            *host = a;
        }
    }
    if (opt->count < 1) opt->count = 1;
    if (opt->timeout_ms < 100) opt->timeout_ms = 100;
    if (opt->interval_ms < 100) opt->interval_ms = 100;
    if (opt->size < 1) opt->size = 1;
    if (opt->size > 1400) opt->size = 1400;
    return !host->empty();
}

static bool resolve_ipv4(const std::wstring& host, sockaddr_in *addr, std::string *display) {
    ADDRINFOW hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    ADDRINFOW *res = nullptr;
    int rc = GetAddrInfoW(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) return false;
    *addr = *(sockaddr_in *)res->ai_addr;
    char ip[INET_ADDRSTRLEN] = {0};
    InetNtopA(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    *display = ip;
    FreeAddrInfoW(res);
    return true;
}

static const char *latency_color(DWORD ms, bool color) {
    if (ms < 50) return ansi_green(color);
    if (ms < 150) return ansi_amber(color);
    return ansi_red(color);
}

static int tool_main(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { help(); return 0; }
    PingOpt opt;
    std::wstring host;
    if (!parse_args(argc, argv, &opt, &host)) {
        help();
        return 1;
    }
    if (opt.color && !opt.force_color) opt.color = ansi_enabled(true);

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "ping: WSAStartup failed\n";
        return 1;
    }

    sockaddr_in addr{};
    std::string ip;
    if (!resolve_ipv4(host, &addr, &ip)) {
        std::cerr << "ping: cannot resolve " << wide_to_utf8(host) << "\n";
        WSACleanup();
        return 1;
    }

    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        std::cerr << "ping: IcmpCreateFile failed\n";
        WSACleanup();
        return 1;
    }

    std::vector<char> payload((size_t)opt.size, 'W');
    DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + (DWORD)payload.size() + 8;
    std::vector<char> reply(reply_size);
    int sent = 0, received = 0;
    DWORD min_ms = 0, max_ms = 0, total_ms = 0;

    std::cout << ansi_bold(opt.color) << "PING " << wide_to_utf8(host)
              << ansi_reset(opt.color) << " (" << ansi_cyan(opt.color) << ip
              << ansi_reset(opt.color) << ") " << opt.size << " bytes\n";

    for (int seq = 1; opt.continuous || seq <= opt.count; ++seq) {
        sent++;
        DWORD rc = IcmpSendEcho(icmp, addr.sin_addr.S_un.S_addr, payload.data(), (WORD)payload.size(),
                                nullptr, reply.data(), reply_size, opt.timeout_ms);
        if (rc > 0) {
            PICMP_ECHO_REPLY er = (PICMP_ECHO_REPLY)reply.data();
            received++;
            DWORD ms = er->RoundTripTime;
            if (received == 1 || ms < min_ms) min_ms = ms;
            if (ms > max_ms) max_ms = ms;
            total_ms += ms;
            std::cout << ansi_green(opt.color) << "reply" << ansi_reset(opt.color)
                      << " from " << ip
                      << ": bytes=" << er->DataSize
                      << " seq=" << seq
                      << " ttl=" << (int)er->Options.Ttl
                      << " time=" << latency_color(ms, opt.color) << ms << "ms" << ansi_reset(opt.color)
                      << "\n";
        } else {
            DWORD err = GetLastError();
            std::cout << ansi_red(opt.color) << "timeout" << ansi_reset(opt.color)
                      << " seq=" << seq << " after " << opt.timeout_ms << "ms";
            if (err != IP_REQ_TIMED_OUT) std::cout << " error=" << err;
            std::cout << "\n";
        }
        if (!opt.continuous && seq >= opt.count) break;
        Sleep((DWORD)opt.interval_ms);
    }

    int loss = sent > 0 ? (int)(((sent - received) * 100) / sent) : 0;
    std::cout << "\n" << ansi_bold(opt.color) << "--- " << wide_to_utf8(host)
              << " ping statistics ---" << ansi_reset(opt.color) << "\n"
              << sent << " transmitted, " << received << " received, "
              << (loss == 0 ? ansi_green(opt.color) : ansi_red(opt.color)) << loss << "% loss"
              << ansi_reset(opt.color) << "\n";
    if (received > 0) {
        std::cout << "rtt min/avg/max = " << min_ms << "/"
                  << (total_ms / (DWORD)received) << "/" << max_ms << " ms\n";
    }

    IcmpCloseHandle(icmp);
    WSACleanup();
    return loss == 100 ? 1 : 0;
}

int wmain(int argc, wchar_t **argv) {
    return run_tool_logged("ping", [&]() { return tool_main(argc, argv); });
}
