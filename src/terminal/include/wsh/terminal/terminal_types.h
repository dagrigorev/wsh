#pragma once

#include <cstddef>

namespace wsh::terminal
{
struct ScreenCell
{
    char32_t codepoint{U' '};
};

struct TerminalSize
{
    std::size_t columns{120};
    std::size_t rows{30};
};
} // namespace wsh::terminal
