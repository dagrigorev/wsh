#include "wsh/vt/ansi_parser.h"

#include <cctype>
#include <string>
#include <vector>

namespace
{
std::vector<int> ParseCsiParams(const std::string& buffer)
{
    std::vector<int> params;
    if (buffer.empty())
    {
        return params;
    }

    std::string current;
    for (char ch : buffer)
    {
        if (ch == ';')
        {
            params.push_back(current.empty() ? 0 : std::stoi(current));
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }

    params.push_back(current.empty() ? 0 : std::stoi(current));
    return params;
}
} // namespace

namespace wsh::vt
{
void AnsiParser::Process(std::string_view data, wsh::terminal::ScreenBuffer& screen)
{
    enum class State
    {
        Ground,
        Escape,
        Csi
    };

    static State state = State::Ground;
    static std::string csiBuffer;

    for (const unsigned char uch : data)
    {
        const char ch = static_cast<char>(uch);

        switch (state)
        {
        case State::Ground:
            if (ch == '\x1b')
            {
                state = State::Escape;
            }
            else if (ch == '\r')
            {
                screen.CarriageReturn();
            }
            else if (ch == '\n')
            {
                screen.LineFeed();
            }
            else if (ch == '\b')
            {
                screen.Backspace();
            }
            else if (static_cast<unsigned char>(ch) >= 0x20)
            {
                screen.PutCodepoint(static_cast<char32_t>(uch));
            }
            break;

        case State::Escape:
            if (ch == '[')
            {
                csiBuffer.clear();
                state = State::Csi;
            }
            else
            {
                state = State::Ground;
            }
            break;

        case State::Csi:
            if ((ch >= '@' && ch <= '~'))
            {
                const auto params = ParseCsiParams(csiBuffer);
                HandleCsi(screen, ch, params);
                csiBuffer.clear();
                state = State::Ground;
            }
            else
            {
                csiBuffer.push_back(ch);
            }
            break;
        }
    }
}

void AnsiParser::HandleCsi(wsh::terminal::ScreenBuffer& screen, char finalByte, const std::vector<int>& params)
{
    const int p0 = params.empty() ? 0 : params[0];

    switch (finalByte)
    {
    case 'A':
        screen.MoveCursorRelative(0, -(p0 == 0 ? 1 : p0));
        break;
    case 'B':
        screen.MoveCursorRelative(0, (p0 == 0 ? 1 : p0));
        break;
    case 'C':
        screen.MoveCursorRelative((p0 == 0 ? 1 : p0), 0);
        break;
    case 'D':
        screen.MoveCursorRelative(-(p0 == 0 ? 1 : p0), 0);
        break;
    case 'H':
    case 'f':
    {
        const int row = params.size() >= 1 ? params[0] : 1;
        const int col = params.size() >= 2 ? params[1] : 1;
        screen.MoveCursor(
            col <= 0 ? 0u : static_cast<std::size_t>(col - 1),
            row <= 0 ? 0u : static_cast<std::size_t>(row - 1));
        break;
    }
    case 'J':
        screen.EraseDisplayFromCursor();
        break;
    case 'K':
        screen.EraseInLineFromCursor();
        break;
    case 'm':
        ApplySgr(screen, params);
        break;
    default:
        break;
    }
}

void AnsiParser::ApplySgr(wsh::terminal::ScreenBuffer& screen, const std::vector<int>& params)
{
    auto attrs = wsh::terminal::TextAttributes::Default();

    if (params.empty())
    {
        screen.SetCurrentAttributes(attrs);
        return;
    }

    for (const int p : params)
    {
        switch (p)
        {
        case 0:
            attrs = wsh::terminal::TextAttributes::Default();
            break;
        case 1:
            attrs.bold = true;
            break;
        case 2:
            attrs.faint = true;
            break;
        case 3:
            attrs.italic = true;
            break;
        case 4:
            attrs.underline = true;
            break;
        case 7:
            attrs.inverse = true;
            break;
        case 22:
            attrs.bold = false;
            attrs.faint = false;
            break;
        case 23:
            attrs.italic = false;
            break;
        case 24:
            attrs.underline = false;
            break;
        case 27:
            attrs.inverse = false;
            break;
        case 39:
            attrs.foreground = wsh::terminal::NamedColor::Default;
            break;
        case 49:
            attrs.background = wsh::terminal::NamedColor::Default;
            break;
        default:
            if (p >= 30 && p <= 37)
            {
                attrs.foreground = SgrToNamedColor(p);
            }
            else if (p >= 40 && p <= 47)
            {
                attrs.background = SgrToNamedColor(p - 10);
            }
            break;
        }
    }

    screen.SetCurrentAttributes(attrs);
}

wsh::terminal::NamedColor AnsiParser::SgrToNamedColor(int sgrCode)
{
    switch (sgrCode)
    {
    case 30: return wsh::terminal::NamedColor::Black;
    case 31: return wsh::terminal::NamedColor::Red;
    case 32: return wsh::terminal::NamedColor::Green;
    case 33: return wsh::terminal::NamedColor::Yellow;
    case 34: return wsh::terminal::NamedColor::Blue;
    case 35: return wsh::terminal::NamedColor::Magenta;
    case 36: return wsh::terminal::NamedColor::Cyan;
    case 37: return wsh::terminal::NamedColor::White;
    default: return wsh::terminal::NamedColor::Default;
    }
}
} // namespace wsh::vt
