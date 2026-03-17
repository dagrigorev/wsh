#pragma once

#include <windows.h>

#include "wsh/common/result.h"
#include "wsh/platform/windows/handle.h"

namespace wsh::platform::windows
{
using PseudoConsoleHandle = HPCON;

[[nodiscard]] wsh::common::Result<PseudoConsoleHandle> CreatePseudoConsoleHandle(
    COORD size,
    HANDLE inputRead,
    HANDLE outputWrite,
    DWORD flags = 0);

void ClosePseudoConsoleHandle(PseudoConsoleHandle handle) noexcept;
[[nodiscard]] wsh::common::Result<void> ResizePseudoConsoleHandle(PseudoConsoleHandle handle, COORD size);
} // namespace wsh::platform::windows
