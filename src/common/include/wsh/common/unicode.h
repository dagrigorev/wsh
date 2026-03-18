#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wsh::common
{
std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

// Converts a sequence of Unicode code points to UTF-8.
std::string CodePointsToUtf8(const std::vector<char32_t>& codePoints);

// Convenience overload for std::u32string-based buffers.
std::string CodePointsToUtf8(const std::u32string& codePoints);
} // namespace wsh::common
