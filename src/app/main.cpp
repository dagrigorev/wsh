#include <chrono>
#include <iostream>
#include <thread>

#include "wsh/terminal/terminal_session.h"

int wmain(int argc, wchar_t* argv[])
{
    std::wstring command = L"cmd.exe";
    if (argc > 1)
    {
        command = argv[1];
    }

    wsh::terminal::TerminalSession session;
    const auto start = session.Start(command, 120, 30);
    if (!start.HasValue())
    {
        std::cerr << "Failed to start terminal session: " << start.GetError().message << '\n';
        return 1;
    }

    std::cout << "WSH Terminal Phase 1 bootstrap OK\n";
    std::cout << "ConPTY session started.\n";

    for (int i = 0; i < 20; ++i)
    {
        const auto pump = session.PumpOutput();
        if (!pump.HasValue())
        {
            std::cerr << "PumpOutput failed: " << pump.GetError().message << '\n';
            return 2;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
