#pragma once

#include <string>
#include <vector>

#include <windows.h>

#include "wsh/common/result.h"
#include "wsh/platform/windows/handle.h"

namespace wsh::platform::windows
{
struct ProcessHandles
{
    UniqueHandle process;
    UniqueHandle thread;
};

struct StartupAttributeList
{
    StartupAttributeList() = default;
    ~StartupAttributeList();

    StartupAttributeList(const StartupAttributeList&) = delete;
    StartupAttributeList& operator=(const StartupAttributeList&) = delete;

    StartupAttributeList(StartupAttributeList&& other) noexcept;
    StartupAttributeList& operator=(StartupAttributeList&& other) noexcept;

    [[nodiscard]] wsh::common::Result<void> Initialize(std::size_t attributeCount);
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return attributes_; }

private:
    LPPROC_THREAD_ATTRIBUTE_LIST attributes_{nullptr};
};

[[nodiscard]] wsh::common::Result<ProcessHandles> LaunchProcess(
    const std::wstring& commandLine,
    STARTUPINFOEXW& startupInfo,
    bool inheritHandles,
    const std::wstring& workingDirectory = {},
    const std::vector<wchar_t>* environmentBlock = nullptr);
} // namespace wsh::platform::windows
