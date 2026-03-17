#include "wsh/platform/windows/handle.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace wsh::platform::windows
{
    UniqueHandle::~UniqueHandle()
    {
        Reset();
    }

    UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.Release())
    {
    }

    UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.Release());
        }
        return *this;
    }

    bool UniqueHandle::Valid() const noexcept
    {
#ifdef _WIN32
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
#else
        return handle_ != nullptr;
#endif
    }

    HANDLE UniqueHandle::Release() noexcept
    {
        HANDLE temp = handle_;
        handle_ = nullptr;
        return temp;
    }

    void UniqueHandle::Reset(HANDLE handle) noexcept
    {
#ifdef _WIN32
        if (Valid())
        {
            ::CloseHandle(handle_);
        }
#endif
        handle_ = handle;
    }
}
