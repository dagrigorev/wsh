#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace wsh::common
{
struct Utf8DecodeResult
{
    char32_t codepoint{U'\0'};
    std::size_t bytesConsumed{0};
    bool complete{false};
    bool valid{false};
};

[[nodiscard]] std::wstring Utf8ToWide(std::string_view value);
[[nodiscard]] std::string WideToUtf8(std::wstring_view value);
[[nodiscard]] Utf8DecodeResult DecodeNextUtf8CodePoint(std::string_view value, std::size_t offset = 0) noexcept;
[[nodiscard]] std::string EncodeUtf8CodePoint(char32_t codepoint);
} // namespace wsh::common
