#include "wsh/platform/windows/handle.h"

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
} // namespace wsh::platform::windows
