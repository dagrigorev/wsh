#pragma once

#include <string>
#include <string_view>

namespace wsh::common
{
[[nodiscard]] std::wstring Utf8ToWide(std::string_view value);
[[nodiscard]] std::string WideToUtf8(std::wstring_view value);
} // namespace wsh::common
