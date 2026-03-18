#pragma once

#include <string>
#include <string_view>

#include "wsh/common/result.h"

namespace wsh::platform::windows
{
struct ProbeResult
{
    std::string snapshotUtf8;
    bool success{false};
};

[[nodiscard]] wsh::common::Result<ProbeResult> RunInteractiveAsciiProbe(
    const std::wstring& shellCommandLine,
    std::string_view asciiCommand,
    short cols = 120,
    short rows = 40,
    int pumpIterations = 24,
    int pumpSleepMs = 40);

[[nodiscard]] wsh::common::Result<ProbeResult> RunPwshEncodedProbe(
    std::wstring_view powerShellExe,
    std::wstring_view scriptUtf16,
    short cols = 120,
    short rows = 40,
    int pumpIterations = 24,
    int pumpSleepMs = 40);

[[nodiscard]] std::wstring BuildPwshEncodedCommandLine(
    std::wstring_view powerShellExe,
    std::wstring_view scriptUtf16);
} // namespace wsh::platform::windows
