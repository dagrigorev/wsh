#include "core/utf.h"

#include <Windows.h>

namespace wsh::core
{
    std::wstring Utf8ToWide(const std::string_view utf8)
    {
        if (utf8.empty())
        {
            return {};
        }

        const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (required <= 0)
        {
            return {};
        }

        std::wstring result(required, L'\0');
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), required);
        return result;
    }

    std::string WideToUtf8(const std::wstring_view wide)
    {
        if (wide.empty())
        {
            return {};
        }

        const int required = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }

        std::string result(required, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), required, nullptr, nullptr);
        return result;
    }
}
