#include "wsh/platform/windows/pipe.h"

#include <windows.h>

namespace wsh::platform::windows
{
UniqueHandle::~UniqueHandle()
{
    reset();
}

UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept
{
    if (this != &other)
    {
        reset(other.release());
    }
    return *this;
}

HANDLE UniqueHandle::release() noexcept
{
    HANDLE value = handle_;
    handle_ = nullptr;
    return value;
}

void UniqueHandle::reset(HANDLE handle) noexcept
{
    if (valid())
    {
        CloseHandle(handle_);
    }
    handle_ = handle;
}

wsh::common::Result<PipePair> CreatePipePair()
{
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;

    if (!CreatePipe(&readHandle, &writeHandle, &attributes, 0))
    {
        return wsh::common::Error{"CreatePipe failed"};
    }

    return PipePair{UniqueHandle{readHandle}, UniqueHandle{writeHandle}};
}
} // namespace wsh::platform::windows
