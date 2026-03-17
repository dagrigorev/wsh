#pragma once

#include <cstddef>
#include <span>
#include <string>

#include <windows.h>

#include "wsh/common/result.h"
#include "wsh/platform/windows/conpty_api.h"
#include "wsh/platform/windows/handle.h"
#include "wsh/platform/windows/process.h"

namespace wsh::conpty
{
class ConptySession
{
public:
    ConptySession() = default;
    ~ConptySession();

    ConptySession(const ConptySession&) = delete;
    ConptySession& operator=(const ConptySession&) = delete;

    ConptySession(ConptySession&&) = delete;
    ConptySession& operator=(ConptySession&&) = delete;

    [[nodiscard]] wsh::common::Result<void> Start(const std::wstring& commandLine, short cols, short rows);
    [[nodiscard]] wsh::common::Result<void> Resize(short cols, short rows);
    [[nodiscard]] wsh::common::Result<void> WriteInput(std::span<const std::byte> data);
    [[nodiscard]] wsh::common::Result<std::string> ReadOutputChunk();
    void Shutdown();

private:
    [[nodiscard]] bool Started() const noexcept { return pseudoConsole_ != nullptr; }

    wsh::platform::windows::PseudoConsoleHandle pseudoConsole_{nullptr};
    wsh::platform::windows::UniqueHandle ptyInputWrite_{};
    wsh::platform::windows::UniqueHandle ptyOutputRead_{};
    wsh::platform::windows::ProcessHandles childProcess_{};
    short cols_{0};
    short rows_{0};
};
} // namespace wsh::conpty
