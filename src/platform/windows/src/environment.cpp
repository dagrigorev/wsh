#include "wsh/platform/windows/environment.h"

#include <windows.h>

namespace wsh::platform::windows
{
std::wstring GetEnvironmentVariableValue(const std::wstring& name)
{
    const DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (size == 0)
    {
        return {};
    }

    std::wstring buffer(static_cast<std::size_t>(size), L'\0');
    GetEnvironmentVariableW(name.c_str(), buffer.data(), size);
    if (!buffer.empty() && buffer.back() == L'\0')
    {
        buffer.pop_back();
    }
    return buffer;
}
} // namespace wsh::platform::windows
