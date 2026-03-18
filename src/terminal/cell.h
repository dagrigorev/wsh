#pragma once

#include <Windows.h>
#include <d2d1.h>

namespace wsh::terminal
{
    struct Cell
    {
        wchar_t glyph = L' ';
        D2D1_COLOR_F foreground = D2D1::ColorF(0.90f, 0.93f, 0.97f, 1.0f);
        D2D1_COLOR_F background = D2D1::ColorF(0.05f, 0.08f, 0.15f, 1.0f);
        bool bold = false;
        bool underline = false;
        bool inverse = false;
    };

    struct Cursor
    {
        int row = 0;
        int column = 0;
        bool visible = true;
    };
}
