#pragma once

#include <cstddef>
#include <cstdint>

namespace wsh::terminal
{
enum class NamedColor : std::uint8_t
{
    Default = 0xFF,
    Black = 0,
    Red = 1,
    Green = 2,
    Yellow = 3,
    Blue = 4,
    Magenta = 5,
    Cyan = 6,
    White = 7
};

struct TextAttributes
{
    NamedColor foreground{NamedColor::Default};
    NamedColor background{NamedColor::Default};
    bool bold{false};
    bool faint{false};
    bool italic{false};
    bool underline{false};
    bool inverse{false};

    static TextAttributes Default() noexcept
    {
        return {};
    }
};

struct ScreenCell
{
    char32_t codepoint{U' '};
    TextAttributes attributes{};
};

struct UsedArea
{
    std::size_t columns{0};
    std::size_t rows{0};
};
} // namespace wsh::terminal
