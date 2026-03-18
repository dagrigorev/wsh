#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <string>
#include <string_view>
#include <vector>

namespace wsh::config
{
    struct Theme
    {
        D2D1_COLOR_F background{ 0.05f, 0.08f, 0.15f, 1.0f };
        D2D1_COLOR_F foreground{ 0.90f, 0.93f, 0.97f, 1.0f };
        D2D1_COLOR_F accent{ 0.35f, 0.66f, 1.0f, 1.0f };
        D2D1_COLOR_F muted{ 0.50f, 0.55f, 0.63f, 1.0f };
        D2D1_COLOR_F statusBackground{ 0.08f, 0.10f, 0.18f, 1.0f };
        D2D1_COLOR_F selection{ 0.17f, 0.31f, 0.54f, 1.0f };
    };

    struct Profile
    {
        std::wstring name;
        std::wstring command;
        std::vector<std::wstring> arguments;
    };

    enum class CursorStyle
    {
        Bar,
        Block,
        Underline
    };

    struct Settings
    {
        std::wstring fontFamily = L"Cascadia Mono";
        float fontSize = 18.0f;
        bool copyOnSelect = true;
        CursorStyle cursorStyle = CursorStyle::Bar;
        Theme theme;
        std::vector<Profile> profiles;
    };

    Settings LoadSettings(const std::wstring& path);
}
