#pragma once

#include <filesystem>

namespace wsh::common
{
[[nodiscard]] std::filesystem::path NormalizePath(const std::filesystem::path& path);
} // namespace wsh::common
