#include "wsh/platform/windows/win_error.h"

#include <windows.h>

#include <string>

namespace
{
std::string WideToUtf8(const std::wstring& value)
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
}

namespace wsh::platform::windows
{
std::string GetLastErrorMessage(const char* prefix)
{
    const DWORD errorCode = GetLastError();

    LPWSTR messageBuffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    std::wstring wideMessage;
    if (length != 0 && messageBuffer != nullptr)
    {
        wideMessage.assign(messageBuffer, messageBuffer + length);
        LocalFree(messageBuffer);
    }
    else
    {
        wideMessage = L"Unknown Win32 error";
    }

    std::string result;
    if (prefix != nullptr)
    {
        result += prefix;
        result += ": ";
    }

    result += WideToUtf8(wideMessage);
    result += " (code=" + std::to_string(errorCode) + ")";
    return result;
}
} // namespace wsh::platform::windows
