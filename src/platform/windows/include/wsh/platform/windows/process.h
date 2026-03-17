#pragma once

#include <string>

#include "wsh/common/result.h"

namespace wsh::platform::windows
{
[[nodiscard]] wsh::common::Result<void> LaunchProcess(const std::wstring& commandLine);
} // namespace wsh::platform::windows
