#pragma once

#include <string>

namespace wsh::platform::windows
{
[[nodiscard]] std::string GetLastErrorMessage(const char* prefix = nullptr);
} // namespace wsh::platform::windows
