#pragma once

namespace wsh::platform::windows
{
struct KeyEvent
{
    unsigned int virtualKey{};
    wchar_t character{};
};
} // namespace wsh::platform::windows
