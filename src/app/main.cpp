#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <windows.h>

#include "wsh/terminal/terminal_session.h"

namespace
{
std::vector<std::byte> ToBytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

void ResizeCurrentConsoleWindowToFit(std::string_view utf8Text)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr)
    {
        return;
    }

    std::size_t maxWidth = 0;
    std::size_t currentWidth = 0;
    std::size_t lineCount = 1;

    for (const char ch : utf8Text)
    {
        if (ch == '\n')
        {
            maxWidth = std::max(maxWidth, currentWidth);
            currentWidth = 0;
            ++lineCount;
        }
        else if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u)
        {
            ++currentWidth;
        }
    }

    maxWidth = std::max(maxWidth, currentWidth);

    const short width = static_cast<short>(std::clamp<std::size_t>(maxWidth + 4, 60, 180));
    const short height = static_cast<short>(std::clamp<std::size_t>(lineCount + 8, 20, 60));

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output, &info))
    {
        return;
    }

    COORD bufferSize{
        static_cast<SHORT>(std::max<int>(info.dwSize.X, width)),
        static_cast<SHORT>(std::max<int>(info.dwSize.Y, height))};

    SetConsoleScreenBufferSize(output, bufferSize);

    SMALL_RECT rect{};
    rect.Left = 0;
    rect.Top = 0;
    rect.Right = static_cast<SHORT>(width - 1);
    rect.Bottom = static_cast<SHORT>(height - 1);
    SetConsoleWindowInfo(output, TRUE, &rect);
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::wstring command = L"cmd.exe /Q /K chcp 65001>nul";
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

    const std::string probe = (command.find(L"cmd") != std::wstring::npos)
        ? "echo WSH_CONPTY_OK\r\necho Привет_Кириллица\r\nexit\r\n"
        : "Write-Output 'WSH_CONPTY_OK'\nWrite-Output 'Привет_Кириллица'\nexit\n";

    const auto writeResult = session.SendInput(ToBytes(probe));
    if (!writeResult.HasValue())
    {
        std::cerr << "Failed to write probe input: " << writeResult.GetError().message << '\n';
        return 2;
    }

    for (int i = 0; i < 50; ++i)
    {
        const auto pump = session.PumpOutput();
        if (!pump.HasValue())
        {
            std::cerr << "PumpOutput failed: " << pump.GetError().message << '\n';
            return 3;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto snapshot = session.GetScreenBuffer().SnapshotUtf8(true);
    ResizeCurrentConsoleWindowToFit(snapshot);

    std::cout << "WSH Terminal Phase 1.5 Unicode bootstrap OK\n";
    std::cout << "Rendered screen snapshot:\n";
    std::cout << "----------------------------------------\n";
    std::cout << snapshot;
    std::cout << "----------------------------------------\n";
    std::cout << "Expected probe text: WSH_CONPTY_OK\n";
    std::cout << "Expected Cyrillic text: Привет_Кириллица\n";
    return 0;
}
