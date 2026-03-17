#pragma once

#include <string>

namespace wsh::platform::windows
{
[[nodiscard]] std::string GetLastErrorMessage();
} // namespace wsh::platform::windows
