#include "platform/clipboard.h"

namespace wsh::platform
{
    std::optional<std::wstring> ReadClipboardText(HWND owner)
    {
        if (!::OpenClipboard(owner))
        {
            return std::nullopt;
        }

        struct ClipboardCloser
        {
            ~ClipboardCloser() { ::CloseClipboard(); }
        } closer;

        HANDLE handle = ::GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr)
        {
            return std::nullopt;
        }

        const auto* text = static_cast<const wchar_t*>(::GlobalLock(handle));
        if (text == nullptr)
        {
            return std::nullopt;
        }

        std::wstring value(text);
        ::GlobalUnlock(handle);
        return value;
    }

    bool WriteClipboardText(HWND owner, const std::wstring& text)
    {
        if (!::OpenClipboard(owner))
        {
            return false;
        }

        struct ClipboardCloser
        {
            ~ClipboardCloser() { ::CloseClipboard(); }
        } closer;

        if (!::EmptyClipboard())
        {
            return false;
        }

        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr)
        {
            return false;
        }

        void* buffer = ::GlobalLock(memory);
        if (buffer == nullptr)
        {
            ::GlobalFree(memory);
            return false;
        }

        memcpy(buffer, text.c_str(), bytes);
        ::GlobalUnlock(memory);

        if (::SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
        {
            ::GlobalFree(memory);
            return false;
        }

        return true;
    }
}
