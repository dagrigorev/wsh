#pragma once

namespace wsh::ui
{
class Keymap
{
public:
    [[nodiscard]] bool IsCopyShortcut(unsigned int virtualKey, bool ctrlPressed) const noexcept;
};
} // namespace wsh::ui
