#pragma once

#include "terminal/screen_buffer.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wsh::terminal
{
    class VtParser
    {
    public:
        explicit VtParser(ScreenBuffer& buffer);
        void Process(std::string_view bytes);
        void SetWindowTitleHandler(std::function<void(const std::wstring&)> handler);

    private:
        void FlushText();
        void HandleEscape(wchar_t ch);
        void HandleCsi(const std::wstring& sequence);
        void HandleOsc(const std::wstring& sequence);
        D2D1_COLOR_F BasicColor(int index, bool bright) const;

        ScreenBuffer& buffer_;
        std::string utf8Pending_;
        std::wstring textBuffer_;
        bool inEscape_ = false;
        bool inCsi_ = false;
        bool sawEscapePrefix_ = false;
        bool inOsc_ = false;
        bool oscSawEscape_ = false;
        std::wstring csiBuffer_;
        std::wstring oscBuffer_;
        std::function<void(const std::wstring&)> windowTitleHandler_;
    };
}
