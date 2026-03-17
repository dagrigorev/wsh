#pragma once

#include <string>

#include "wsh/common/result.h"

namespace wsh::platform::windows
{
[[nodiscard]] wsh::common::Result<void> SetClipboardText(const std::wstring& text);
} // namespace wsh::platform::windows
