#include "wsh/terminal/terminal_session.h"

namespace wsh::terminal
{
wsh::common::Result<void> TerminalSession::Start(const std::wstring& commandLine, short cols, short rows)
{
    screenBuffer_.Resize(static_cast<std::size_t>(cols), static_cast<std::size_t>(rows));
    return conpty_.Start(commandLine, cols, rows);
}

wsh::common::Result<void> TerminalSession::PumpOutput()
{
    const auto output = conpty_.ReadOutputChunk();
    if (!output.HasValue())
    {
        return output.GetError();
    }

    parser_.Process(output.Value(), screenBuffer_);
    return {};
}

wsh::common::Result<void> TerminalSession::SendInput(std::span<const std::byte> data)
{
    return conpty_.WriteInput(data);
}

wsh::common::Result<void> TerminalSession::Resize(short cols, short rows)
{
    screenBuffer_.Resize(static_cast<std::size_t>(cols), static_cast<std::size_t>(rows));
    return conpty_.Resize(cols, rows);
}
} // namespace wsh::terminal
