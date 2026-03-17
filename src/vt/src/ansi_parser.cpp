#include "wsh/vt/ansi_parser.h"

namespace wsh::vt
{
void AnsiParser::Process(std::string_view data, wsh::terminal::ScreenBuffer& screen)
{
    for (const char ch : data)
    {
        switch (ch)
        {
        case '\r':
            screen.CarriageReturn();
            break;
        case '\n':
            screen.NewLine();
            break;
        default:
            if (static_cast<unsigned char>(ch) >= 0x20)
            {
                screen.PutChar(static_cast<unsigned char>(ch));
            }
            break;
        }
    }
}
} // namespace wsh::vt
