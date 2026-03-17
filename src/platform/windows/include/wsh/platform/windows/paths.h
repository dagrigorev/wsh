#pragma once

#include <filesystem>

namespace wsh::platform::windows
{
[[nodiscard]] std::filesystem::path GetHomeDirectory();
} // namespace wsh::platform::windows
