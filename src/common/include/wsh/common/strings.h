#pragma once

#include <string>
#include <string_view>

namespace wsh::common
{
[[nodiscard]] std::string Trim(std::string_view value);
} // namespace wsh::common
