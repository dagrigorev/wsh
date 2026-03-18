#include "wsh/platform/windows/execution_helpers.h"

#include <chrono>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "wsh/common/unicode.h"
#include "wsh/conpty/conpty_session.h"
#include "wsh/terminal/terminal_session.h"

namespace
{
std::vector<std::byte> ToByteVector(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());

    for (const unsigned char ch : text)
    {
        bytes.push_back(static_cast<std::byte>(ch));
    }

    return bytes;
}

std::wstring EncodeBase64Utf16Le(std::wstring_view text)
{
    static constexpr wchar_t kAlphabet[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const auto byteCount = text.size() * sizeof(wchar_t);
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());

    std::wstring out;
    out.reserve(((byteCount + 2) / 3) * 4);

    for (std::size_t i = 0; i < byteCount; i += 3)
    {
        const unsigned int b0 = raw[i];
        const unsigned int b1 = (i + 1 < byteCount) ? raw[i + 1] : 0U;
        const unsigned int b2 = (i + 2 < byteCount) ? raw[i + 2] : 0U;

        const unsigned int triple = (b0 << 16U) | (b1 << 8U) | b2;

        out.push_back(kAlphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(triple >> 12U) & 0x3FU]);
        out.push_back((i + 1 < byteCount) ? kAlphabet[(triple >> 6U) & 0x3FU] : L'=');
        out.push_back((i + 2 < byteCount) ? kAlphabet[triple & 0x3FU] : L'=');
    }

    return out;
}

void PumpTerminal(wsh::terminal::TerminalSession& terminal, int iterations, int sleepMs)
{
    for (int i = 0; i < iterations; ++i)
    {
        (void)terminal.PumpOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}
} // namespace

namespace wsh::platform::windows
{
wsh::common::Result<ProbeResult> RunInteractiveAsciiProbe(
    const std::wstring& shellCommandLine,
    std::string_view asciiCommand,
    short cols,
    short rows,
    int pumpIterations,
    int pumpSleepMs)
{
    wsh::terminal::TerminalSession terminal;
    if (const auto started = terminal.Start(shellCommandLine, cols, rows); !started.HasValue())
    {
        return started.GetError();
    }

    PumpTerminal(terminal, pumpIterations / 3, pumpSleepMs);

    const auto input = ToByteVector(asciiCommand);
    if (const auto sent = terminal.SendInput(std::span<const std::byte>(input.data(), input.size())); !sent.HasValue())
    {
        return sent.GetError();
    }

    PumpTerminal(terminal, pumpIterations, pumpSleepMs);

    ProbeResult result;
    result.snapshotUtf8 = terminal.GetScreenBuffer().ToUtf8String();
    result.success = true;
    return result;
}

std::wstring BuildPwshEncodedCommandLine(
    std::wstring_view powerShellExe,
    std::wstring_view scriptUtf16)
{
    const auto encoded = EncodeBase64Utf16Le(scriptUtf16);
    std::wstring commandLine;
    commandLine.reserve(powerShellExe.size() + encoded.size() + 64);
    commandLine.append(powerShellExe);
    commandLine.append(L" -NoLogo -NoProfile -EncodedCommand ");
    commandLine.append(encoded);
    return commandLine;
}

wsh::common::Result<ProbeResult> RunPwshEncodedProbe(
    std::wstring_view powerShellExe,
    std::wstring_view scriptUtf16,
    short cols,
    short rows,
    int pumpIterations,
    int pumpSleepMs)
{
    wsh::terminal::TerminalSession terminal;
    const auto commandLine = BuildPwshEncodedCommandLine(powerShellExe, scriptUtf16);

    if (const auto started = terminal.Start(commandLine, cols, rows); !started.HasValue())
    {
        return started.GetError();
    }

    PumpTerminal(terminal, pumpIterations, pumpSleepMs);

    ProbeResult result;
    result.snapshotUtf8 = terminal.GetScreenBuffer().ToUtf8String();
    result.success = true;
    return result;
}
} // namespace wsh::platform::windows
