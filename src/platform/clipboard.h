#pragma once

#include <Windows.h>
#include <optional>
#include <string>

namespace wsh::platform
{
    std::optional<std::wstring> ReadClipboardText(HWND owner);
    bool WriteClipboardText(HWND owner, const std::wstring& text);
}
