#include "wsh/platform/windows/pipe.h"

#include <windows.h>

#include "wsh/platform/windows/win_error.h"

namespace wsh::platform::windows
{
wsh::common::Result<PipePair> CreatePipePair()
{
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;

    if (!CreatePipe(&readHandle, &writeHandle, &attributes, 0))
    {
        return wsh::common::Error{GetLastErrorMessage("CreatePipe failed")};
    }

    return PipePair{UniqueHandle{readHandle}, UniqueHandle{writeHandle}};
}
} // namespace wsh::platform::windows
