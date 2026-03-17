#include "wsh/conpty/conpty_session.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "wsh/platform/windows/pipe.h"
#include "wsh/platform/windows/win_error.h"

namespace wsh::conpty
{
ConptySession::~ConptySession()
{
    Shutdown();
}

wsh::common::Result<void> ConptySession::Start(const std::wstring& commandLine, short cols, short rows)
{
    Shutdown();

    auto inputPipe = wsh::platform::windows::CreatePipePair();
    if (!inputPipe.HasValue())
    {
        return inputPipe.GetError();
    }

    auto outputPipe = wsh::platform::windows::CreatePipePair();
    if (!outputPipe.HasValue())
    {
        return outputPipe.GetError();
    }

    auto input = std::move(inputPipe.Value());
    auto output = std::move(outputPipe.Value());

    const COORD size{cols, rows};
    auto pseudoConsole = wsh::platform::windows::CreatePseudoConsoleHandle(
        size,
        input.read.get(),
        output.write.get());

    if (!pseudoConsole.HasValue())
    {
        return pseudoConsole.GetError();
    }

    input.read.reset();
    output.write.reset();

    wsh::platform::windows::StartupAttributeList attributeList;
    auto initAttributes = attributeList.Initialize(1);
    if (!initAttributes.HasValue())
    {
        wsh::platform::windows::ClosePseudoConsoleHandle(pseudoConsole.Value());
        return initAttributes.GetError();
    }

    auto pseudoConsoleValue = pseudoConsole.Value();
    if (!UpdateProcThreadAttribute(
            attributeList.get(),
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            pseudoConsoleValue,
            sizeof(pseudoConsoleValue),
            nullptr,
            nullptr))
    {
        const auto error = wsh::platform::windows::GetLastErrorMessage("UpdateProcThreadAttribute failed");
        wsh::platform::windows::ClosePseudoConsoleHandle(pseudoConsoleValue);
        return wsh::common::Error{error};
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.lpAttributeList = attributeList.get();

    auto launch = wsh::platform::windows::LaunchProcess(commandLine, startupInfo, false);
    if (!launch.HasValue())
    {
        wsh::platform::windows::ClosePseudoConsoleHandle(pseudoConsoleValue);
        return launch.GetError();
    }

    pseudoConsole_ = pseudoConsoleValue;
    ptyInputWrite_ = std::move(input.write);
    ptyOutputRead_ = std::move(output.read);
    childProcess_ = std::move(launch.Value());
    cols_ = cols;
    rows_ = rows;

    return {};
}

wsh::common::Result<void> ConptySession::Resize(short cols, short rows)
{
    if (!Started())
    {
        return wsh::common::Error{"ConPTY session is not started"};
    }

    const auto result = wsh::platform::windows::ResizePseudoConsoleHandle(pseudoConsole_, COORD{cols, rows});
    if (result.HasValue())
    {
        cols_ = cols;
        rows_ = rows;
    }
    return result;
}

wsh::common::Result<void> ConptySession::WriteInput(std::span<const std::byte> data)
{
    if (!ptyInputWrite_.valid())
    {
        return wsh::common::Error{"ConPTY input pipe is not available"};
    }

    DWORD written = 0;
    const auto* buffer = reinterpret_cast<const void*>(data.data());
    const auto size = static_cast<DWORD>(data.size_bytes());
    if (!WriteFile(ptyInputWrite_.get(), buffer, size, &written, nullptr))
    {
        return wsh::common::Error{wsh::platform::windows::GetLastErrorMessage("WriteFile to ConPTY failed")};
    }

    return {};
}

wsh::common::Result<std::string> ConptySession::ReadOutputChunk()
{
    if (!ptyOutputRead_.valid())
    {
        return wsh::common::Error{"ConPTY output pipe is not available"};
    }

    DWORD available = 0;
    if (!PeekNamedPipe(ptyOutputRead_.get(), nullptr, 0, nullptr, &available, nullptr))
    {
        const auto lastError = GetLastError();
        if (lastError == ERROR_BROKEN_PIPE)
        {
            return std::string{};
        }

        return wsh::common::Error{wsh::platform::windows::GetLastErrorMessage("PeekNamedPipe failed")};
    }

    if (available == 0)
    {
        return std::string{};
    }

    std::string chunk(static_cast<std::size_t>(available), '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(ptyOutputRead_.get(), chunk.data(), available, &bytesRead, nullptr))
    {
        const auto lastError = GetLastError();
        if (lastError == ERROR_BROKEN_PIPE)
        {
            return std::string{};
        }

        return wsh::common::Error{wsh::platform::windows::GetLastErrorMessage("ReadFile from ConPTY failed")};
    }

    chunk.resize(static_cast<std::size_t>(bytesRead));
    return chunk;
}

void ConptySession::Shutdown()
{
    ptyInputWrite_.reset();
    ptyOutputRead_.reset();
    childProcess_.thread.reset();

    if (childProcess_.process.valid())
    {
        TerminateProcess(childProcess_.process.get(), 0);
        childProcess_.process.reset();
    }

    if (pseudoConsole_ != nullptr)
    {
        wsh::platform::windows::ClosePseudoConsoleHandle(pseudoConsole_);
        pseudoConsole_ = nullptr;
    }

    cols_ = 0;
    rows_ = 0;
}
} // namespace wsh::conpty
