#pragma once

#include <string_view>

#include "wsh/terminal/screen_buffer.h"

namespace wsh::vt
{
class AnsiParser
{
public:
    void Process(std::string_view data, wsh::terminal::ScreenBuffer& screen);
};
} // namespace wsh::vt
