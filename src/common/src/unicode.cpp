#include "wsh/common/unicode.h"

#include <Windows.h>

#include <stdexcept>

namespace
{
std::string EncodeCodePointToUtf8(char32_t cp)
{
    std::string out;

    if (cp <= 0x7F)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }

    return out;
}
} // namespace

namespace wsh::common
{
std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);

    if (required <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');

    const int converted = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        required);

    if (converted <= 0)
    {
        return {};
    }

    return wide;
}

std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty())
    {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');

    const int converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        utf8.data(),
        required,
        nullptr,
        nullptr);

    if (converted <= 0)
    {
        return {};
    }

    return utf8;
}

std::string CodePointsToUtf8(const std::vector<char32_t>& codePoints)
{
    std::string out;
    out.reserve(codePoints.size());

    for (const char32_t cp : codePoints)
    {
        out += EncodeCodePointToUtf8(cp);
    }

    return out;
}

std::string CodePointsToUtf8(const std::u32string& codePoints)
{
    std::string out;
    out.reserve(codePoints.size());

    for (const char32_t cp : codePoints)
    {
        out += EncodeCodePointToUtf8(cp);
    }

    return out;
}
} // namespace wsh::common
