#include "terminal/vt_parser.h"
#include "core/utf.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace wsh::terminal
{
    namespace
    {
        D2D1_COLOR_F IndexedColor(const int index)
        {
            static const int cube[] = { 0, 95, 135, 175, 215, 255 };
            if (index < 16)
            {
                const bool bright = index >= 8;
                const int base = bright ? index - 8 : index;
                static const D2D1_COLOR_F normal[] = {
                    D2D1::ColorF(0.05f, 0.08f, 0.15f, 1.0f),
                    D2D1::ColorF(0.85f, 0.33f, 0.33f, 1.0f),
                    D2D1::ColorF(0.39f, 0.78f, 0.45f, 1.0f),
                    D2D1::ColorF(0.87f, 0.76f, 0.34f, 1.0f),
                    D2D1::ColorF(0.34f, 0.60f, 0.94f, 1.0f),
                    D2D1::ColorF(0.76f, 0.49f, 0.87f, 1.0f),
                    D2D1::ColorF(0.33f, 0.78f, 0.85f, 1.0f),
                    D2D1::ColorF(0.90f, 0.93f, 0.97f, 1.0f)
                };
                static const D2D1_COLOR_F brightMap[] = {
                    D2D1::ColorF(0.20f, 0.24f, 0.33f, 1.0f),
                    D2D1::ColorF(1.00f, 0.45f, 0.43f, 1.0f),
                    D2D1::ColorF(0.52f, 0.90f, 0.56f, 1.0f),
                    D2D1::ColorF(0.98f, 0.85f, 0.44f, 1.0f),
                    D2D1::ColorF(0.45f, 0.74f, 1.0f, 1.0f),
                    D2D1::ColorF(0.89f, 0.63f, 0.98f, 1.0f),
                    D2D1::ColorF(0.47f, 0.88f, 0.95f, 1.0f),
                    D2D1::ColorF(1.00f, 1.00f, 1.00f, 1.0f)
                };
                return bright ? brightMap[base] : normal[base];
            }
            if (index >= 16 && index <= 231)
            {
                const int value = index - 16;
                const int r = cube[(value / 36) % 6];
                const int g = cube[(value / 6) % 6];
                const int b = cube[value % 6];
                return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
            }
            const int gray = std::clamp(8 + (index - 232) * 10, 0, 255);
            return D2D1::ColorF(gray / 255.0f, gray / 255.0f, gray / 255.0f, 1.0f);
        }
    }

    VtParser::VtParser(ScreenBuffer& buffer)
        : buffer_(buffer)
    {
    }

    void VtParser::SetWindowTitleHandler(std::function<void(const std::wstring&)> handler)
    {
        windowTitleHandler_ = std::move(handler);
    }

    void VtParser::FlushText()
    {
        const Cell style = buffer_.CurrentStyle();
        for (const wchar_t ch : textBuffer_)
        {
            buffer_.PutChar(ch, style);
        }
        textBuffer_.clear();
    }

    void VtParser::Process(const std::string_view bytes)
    {
        for (const unsigned char byte : bytes)
        {
            if (inEscape_)
            {
                HandleEscape(static_cast<wchar_t>(byte));
                continue;
            }

            switch (byte)
            {
            case 0x1B:
                FlushText();
                inEscape_ = true;
                break;
            case '\r':
                FlushText();
                buffer_.CarriageReturn();
                break;
            case '\n':
                FlushText();
                buffer_.LineFeed();
                break;
            case '\b':
                FlushText();
                buffer_.Backspace();
                break;
            case '\t':
                FlushText();
                buffer_.Tab();
                break;
            default:
                utf8Pending_.push_back(static_cast<char>(byte));
                {
                    const std::wstring decoded = wsh::core::Utf8ToWide(utf8Pending_);
                    if (!decoded.empty())
                    {
                        textBuffer_ += decoded;
                        utf8Pending_.clear();
                    }
                }
                break;
            }
        }

        FlushText();
    }

    void VtParser::HandleEscape(const wchar_t ch)
    {
        if (inOsc_)
        {
            if (oscSawEscape_)
            {
                if (ch == L'\\')
                {
                    HandleOsc(oscBuffer_);
                    oscBuffer_.clear();
                    oscSawEscape_ = false;
                    inOsc_ = false;
                    inEscape_ = false;
                    return;
                }

                oscBuffer_.push_back(L'\x1B');
                oscSawEscape_ = false;
            }

            if (ch == 0x07)
            {
                HandleOsc(oscBuffer_);
                oscBuffer_.clear();
                inOsc_ = false;
                inEscape_ = false;
                return;
            }

            if (ch == 0x1B)
            {
                oscSawEscape_ = true;
                return;
            }

            oscBuffer_.push_back(ch);
            return;
        }

        if (!inCsi_)
        {
            if (ch == L'[')
            {
                inCsi_ = true;
                csiBuffer_.clear();
                sawEscapePrefix_ = false;
                return;
            }

            if (ch == L']')
            {
                inOsc_ = true;
                oscBuffer_.clear();
                oscSawEscape_ = false;
                return;
            }

            if (ch == L'7')
            {
                buffer_.SaveCursor();
                inEscape_ = false;
                return;
            }

            if (ch == L'8')
            {
                buffer_.RestoreCursor();
                inEscape_ = false;
                return;
            }

            inEscape_ = false;
            return;
        }

        csiBuffer_.push_back(ch);
        if (!sawEscapePrefix_ && (ch == L'?' || ch == L'>' || ch == L'!'))
        {
            sawEscapePrefix_ = true;
            return;
        }

        if ((ch >= L'@' && ch <= L'~') || std::iswalpha(ch))
        {
            HandleCsi(csiBuffer_);
            inCsi_ = false;
            inEscape_ = false;
            sawEscapePrefix_ = false;
        }
    }

    D2D1_COLOR_F VtParser::BasicColor(const int index, const bool bright) const
    {
        return IndexedColor((bright ? 8 : 0) + index);
    }

    void VtParser::HandleOsc(const std::wstring& sequence)
    {
        const auto separator = sequence.find(L';');
        if (separator == std::wstring::npos)
        {
            return;
        }

        const std::wstring command = sequence.substr(0, separator);
        const std::wstring payload = sequence.substr(separator + 1);
        if ((command == L"0" || command == L"2") && windowTitleHandler_)
        {
            windowTitleHandler_(payload);
        }
    }

    void VtParser::HandleCsi(const std::wstring& sequence)
    {
        if (sequence.empty())
        {
            return;
        }

        const wchar_t command = sequence.back();
        const std::wstring paramsText = sequence.substr(0, sequence.size() - 1);
        const bool privateMode = !paramsText.empty() && (paramsText.front() == L'?' || paramsText.front() == L'>');

        auto parse_param = [](std::wstring item) -> int
        {
            item.erase(std::remove_if(item.begin(), item.end(), [](const wchar_t ch)
            {
                return !(ch >= L'0' && ch <= L'9') && ch != L'-';
            }), item.end());

            if (item.empty() || item == L"-")
            {
                return 0;
            }

            try
            {
                return std::stoi(item);
            }
            catch (...)
            {
                return 0;
            }
        };

        std::vector<int> params;
        std::wstringstream stream(paramsText);
        std::wstring item;
        while (std::getline(stream, item, L';'))
        {
            params.push_back(parse_param(item));
        }
        if (params.empty())
        {
            params.push_back(0);
        }

        switch (command)
        {
        case L'H':
        case L'f':
        {
            const int row = params.size() > 0 ? std::max(1, params[0]) - 1 : 0;
            const int column = params.size() > 1 ? std::max(1, params[1]) - 1 : 0;
            buffer_.MoveCursor(row, column);
            break;
        }
        case L'A':
            buffer_.MoveCursor(buffer_.GetCursor().row - std::max(1, params[0]), buffer_.GetCursor().column);
            break;
        case L'B':
            buffer_.MoveCursor(buffer_.GetCursor().row + std::max(1, params[0]), buffer_.GetCursor().column);
            break;
        case L'C':
            buffer_.MoveCursor(buffer_.GetCursor().row, buffer_.GetCursor().column + std::max(1, params[0]));
            break;
        case L'D':
            buffer_.MoveCursor(buffer_.GetCursor().row, buffer_.GetCursor().column - std::max(1, params[0]));
            break;
        case L'E':
            buffer_.MoveCursor(buffer_.GetCursor().row + std::max(1, params[0]), 0);
            break;
        case L'F':
            buffer_.MoveCursor(buffer_.GetCursor().row - std::max(1, params[0]), 0);
            break;
        case L'G':
            buffer_.MoveCursor(buffer_.GetCursor().row, std::max(1, params[0]) - 1);
            break;
        case L'd':
            buffer_.MoveCursor(std::max(1, params[0]) - 1, buffer_.GetCursor().column);
            break;
        case L'J':
            buffer_.ClearDisplay(params[0]);
            break;
        case L'K':
            buffer_.ClearLine(params[0]);
            break;
        case L'm':
            for (size_t i = 0; i < params.size(); ++i)
            {
                const int value = params[i];
                if (value == 0) buffer_.ResetAttributes();
                else if (value == 1) buffer_.SetBold(true);
                else if (value == 4) buffer_.SetUnderline(true);
                else if (value == 7) buffer_.SetInverse(true);
                else if (value == 22) buffer_.SetBold(false);
                else if (value == 24) buffer_.SetUnderline(false);
                else if (value == 27) buffer_.SetInverse(false);
                else if (value == 39) buffer_.ResetForeground();
                else if (value == 49) buffer_.ResetBackground();
                else if (value >= 30 && value <= 37) buffer_.SetForeground(BasicColor(value - 30, false));
                else if (value >= 90 && value <= 97) buffer_.SetForeground(BasicColor(value - 90, true));
                else if (value >= 40 && value <= 47) buffer_.SetBackground(BasicColor(value - 40, false));
                else if (value >= 100 && value <= 107) buffer_.SetBackground(BasicColor(value - 100, true));
                else if ((value == 38 || value == 48) && i + 1 < params.size())
                {
                    const bool foreground = value == 38;
                    const int mode = params[++i];
                    if (mode == 5 && i + 1 < params.size())
                    {
                        const D2D1_COLOR_F color = IndexedColor(std::clamp(params[++i], 0, 255));
                        if (foreground) buffer_.SetForeground(color); else buffer_.SetBackground(color);
                    }
                    else if (mode == 2 && i + 3 < params.size())
                    {
                        const float r = std::clamp(params[++i], 0, 255) / 255.0f;
                        const float g = std::clamp(params[++i], 0, 255) / 255.0f;
                        const float b = std::clamp(params[++i], 0, 255) / 255.0f;
                        const D2D1_COLOR_F color = D2D1::ColorF(r, g, b, 1.0f);
                        if (foreground) buffer_.SetForeground(color); else buffer_.SetBackground(color);
                    }
                }
            }
            break;
        case L's':
            buffer_.SaveCursor();
            break;
        case L'u':
            buffer_.RestoreCursor();
            break;
        case L'h':
            if (privateMode && !params.empty())
            {
                if (params[0] == 25) buffer_.SetCursorVisible(true);
                else if (params[0] == 1049 || params[0] == 1047 || params[0] == 47) buffer_.EnterAlternateScreen();
                else if (params[0] == 2004) buffer_.SetBracketedPasteMode(true);
            }
            break;
        case L'l':
            if (privateMode && !params.empty())
            {
                if (params[0] == 25) buffer_.SetCursorVisible(false);
                else if (params[0] == 1049 || params[0] == 1047 || params[0] == 47) buffer_.LeaveAlternateScreen();
                else if (params[0] == 2004) buffer_.SetBracketedPasteMode(false);
            }
            break;
        default:
            break;
        }
    }
}
