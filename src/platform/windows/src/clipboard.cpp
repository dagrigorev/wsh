#include "wsh/platform/windows/clipboard.h"

namespace wsh::platform::windows
{
wsh::common::Result<void> SetClipboardText(const std::wstring&)
{
    return {};
}
} // namespace wsh::platform::windows
