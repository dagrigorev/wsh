#include "wsh/vt/ansi_parser.h"

#include <string>

#include "wsh/common/unicode.h"
#include "wsh/terminal/screen_buffer.h"

namespace wsh::vt
{
void AnsiParser::Process(std::string_view data, wsh::terminal::ScreenBuffer& screen)
{
    for (const char ch : data)
    {
        const auto uch = static_cast<unsigned char>(ch);

        switch (state_)
        {
        case State::Escape:
            if (uch == '[')
            {
                state_ = State::Csi;
            }
            else
            {
                state_ = State::Text;
            }
            continue;

        case State::Csi:
            if (uch >= 0x40 && uch <= 0x7E)
            {
                state_ = State::Text;
            }
            continue;

        case State::Text:
            break;
        }

        if (uch == 0x1B)
        {
            state_ = State::Escape;
            continue;
        }

        if (ch == '\r')
        {
            pendingUtf8_.clear();
            screen.CarriageReturn();
            continue;
        }

        if (ch == '\n')
        {
            pendingUtf8_.clear();
            screen.NewLine();
            continue;
        }

        if (ch == '\b')
        {
            pendingUtf8_.clear();
            screen.Backspace();
            continue;
        }

        if (uch < 0x20)
        {
            continue;
        }

        pendingUtf8_.push_back(ch);

        while (!pendingUtf8_.empty())
        {
            const auto decoded = wsh::common::DecodeNextUtf8CodePoint(pendingUtf8_);
            if (!decoded.complete)
            {
                break;
            }

            if (decoded.bytesConsumed == 0)
            {
                pendingUtf8_.clear();
                break;
            }

            screen.PutChar(decoded.valid ? decoded.codepoint : U'?');
            pendingUtf8_.erase(0, decoded.bytesConsumed);
        }
    }
}
} // namespace wsh::vt
