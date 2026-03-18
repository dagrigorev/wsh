#pragma once

#include <cstddef>

namespace wsh::terminal
{
struct CursorState
{
    std::size_t row{0};
    std::size_t column{0};
    bool visible{true};
};
} // namespace wsh::terminal
