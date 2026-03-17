#include "wsh/platform/windows/conpty_api.h"

#include <consoleapi3.h>

#include "wsh/platform/windows/win_error.h"

namespace wsh::platform::windows
{
wsh::common::Result<PseudoConsoleHandle> CreatePseudoConsoleHandle(
    COORD size,
    HANDLE inputRead,
    HANDLE outputWrite,
    DWORD flags)
{
    HPCON handle = nullptr;
    const HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, flags, &handle);
    if (FAILED(hr))
    {
        SetLastError(HRESULT_CODE(hr));
        return wsh::common::Error{GetLastErrorMessage("CreatePseudoConsole failed")};
    }

    return handle;
}

void ClosePseudoConsoleHandle(PseudoConsoleHandle handle) noexcept
{
    if (handle != nullptr)
    {
        ClosePseudoConsole(handle);
    }
}

wsh::common::Result<void> ResizePseudoConsoleHandle(PseudoConsoleHandle handle, COORD size)
{
    const HRESULT hr = ResizePseudoConsole(handle, size);
    if (FAILED(hr))
    {
        SetLastError(HRESULT_CODE(hr));
        return wsh::common::Error{GetLastErrorMessage("ResizePseudoConsole failed")};
    }

    return {};
}
} // namespace wsh::platform::windows
