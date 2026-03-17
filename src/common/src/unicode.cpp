#include "wsh/common/unicode.h"

#include <windows.h>

namespace wsh::common
{
std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

Utf8DecodeResult DecodeNextUtf8CodePoint(std::string_view value, std::size_t offset) noexcept
{
    if (offset >= value.size())
    {
        return {};
    }

    const auto b0 = static_cast<unsigned char>(value[offset]);
    if (b0 < 0x80)
    {
        return Utf8DecodeResult{static_cast<char32_t>(b0), 1, true, true};
    }

    auto continuation = [&](std::size_t index) -> int
    {
        if (index >= value.size())
        {
            return -1;
        }

        const auto byte = static_cast<unsigned char>(value[index]);
        if ((byte & 0xC0u) != 0x80u)
        {
            return -2;
        }

        return static_cast<int>(byte & 0x3Fu);
    };

    if ((b0 & 0xE0u) == 0xC0u)
    {
        if (offset + 1 >= value.size())
        {
            return Utf8DecodeResult{U'\0', 0, false, false};
        }

        const int c1 = continuation(offset + 1);
        if (c1 < 0)
        {
            return Utf8DecodeResult{U'?', 1, true, false};
        }

        const char32_t codepoint = static_cast<char32_t>(((b0 & 0x1Fu) << 6) | c1);
        return Utf8DecodeResult{codepoint, 2, true, true};
    }

    if ((b0 & 0xF0u) == 0xE0u)
    {
        if (offset + 2 >= value.size())
        {
            return Utf8DecodeResult{U'\0', 0, false, false};
        }

        const int c1 = continuation(offset + 1);
        const int c2 = continuation(offset + 2);
        if (c1 < 0 || c2 < 0)
        {
            return Utf8DecodeResult{U'?', 1, true, false};
        }

        const char32_t codepoint = static_cast<char32_t>(((b0 & 0x0Fu) << 12) | (c1 << 6) | c2);
        return Utf8DecodeResult{codepoint, 3, true, true};
    }

    if ((b0 & 0xF8u) == 0xF0u)
    {
        if (offset + 3 >= value.size())
        {
            return Utf8DecodeResult{U'\0', 0, false, false};
        }

        const int c1 = continuation(offset + 1);
        const int c2 = continuation(offset + 2);
        const int c3 = continuation(offset + 3);
        if (c1 < 0 || c2 < 0 || c3 < 0)
        {
            return Utf8DecodeResult{U'?', 1, true, false};
        }

        const char32_t codepoint = static_cast<char32_t>(((b0 & 0x07u) << 18) | (c1 << 12) | (c2 << 6) | c3);
        return Utf8DecodeResult{codepoint, 4, true, true};
    }

    return Utf8DecodeResult{U'?', 1, true, false};
}

std::string EncodeUtf8CodePoint(char32_t codepoint)
{
    std::string result;

    if (codepoint <= 0x7F)
    {
        result.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    return result;
}
} // namespace wsh::common
