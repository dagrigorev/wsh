#include "wsh/ui/keymap.h"

namespace wsh::ui
{
bool Keymap::IsCopyShortcut(unsigned int virtualKey, bool ctrlPressed) const noexcept
{
    return ctrlPressed && virtualKey == 'C';
}
} // namespace wsh::ui
