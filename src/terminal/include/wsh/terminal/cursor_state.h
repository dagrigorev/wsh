#pragma once

#include <cstddef>

namespace wsh::terminal
{
struct CursorState
{
    std::size_t row{};
    std::size_t column{};
};
} // namespace wsh::terminal
