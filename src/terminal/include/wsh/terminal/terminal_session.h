#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "wsh/common/result.h"
#include "wsh/conpty/conpty_session.h"
#include "wsh/terminal/screen_buffer.h"
#include "wsh/vt/ansi_parser.h"

namespace wsh::terminal
{
class TerminalSession
{
public:
    [[nodiscard]] wsh::common::Result<void> Start(const std::wstring& commandLine, short cols, short rows);
    [[nodiscard]] wsh::common::Result<void> PumpOutput();
    [[nodiscard]] wsh::common::Result<void> SendInput(std::span<const std::byte> data);
    [[nodiscard]] wsh::common::Result<void> Resize(short cols, short rows);

    [[nodiscard]] const ScreenBuffer& GetScreenBuffer() const noexcept { return screenBuffer_; }

private:
    wsh::conpty::ConptySession conpty_{};
    wsh::vt::AnsiParser parser_{};
    ScreenBuffer screenBuffer_{};
};
} // namespace wsh::terminal
