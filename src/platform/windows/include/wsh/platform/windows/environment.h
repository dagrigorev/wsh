#pragma once

#include <string>

namespace wsh::platform::windows
{
[[nodiscard]] std::wstring GetEnvironmentVariableValue(const std::wstring& name);
} // namespace wsh::platform::windows
