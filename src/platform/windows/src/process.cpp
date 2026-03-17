#include "wsh/platform/windows/process.h"

#include <new>
#include <windows.h>

#include <vector>

#include "wsh/platform/windows/win_error.h"

namespace wsh::platform::windows
{
StartupAttributeList::~StartupAttributeList()
{
    if (attributes_ != nullptr)
    {
        DeleteProcThreadAttributeList(attributes_);
        ::operator delete(attributes_);
        attributes_ = nullptr;
    }
}

StartupAttributeList::StartupAttributeList(StartupAttributeList&& other) noexcept : attributes_(other.attributes_)
{
    other.attributes_ = nullptr;
}

StartupAttributeList& StartupAttributeList::operator=(StartupAttributeList&& other) noexcept
{
    if (this != &other)
    {
        if (attributes_ != nullptr)
        {
            DeleteProcThreadAttributeList(attributes_);
            ::operator delete(attributes_);
        }

        attributes_ = other.attributes_;
        other.attributes_ = nullptr;
    }
    return *this;
}

wsh::common::Result<void> StartupAttributeList::Initialize(std::size_t attributeCount)
{
    if (attributes_ != nullptr)
    {
        DeleteProcThreadAttributeList(attributes_);
        ::operator delete(attributes_);
        attributes_ = nullptr;
    }

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, static_cast<DWORD>(attributeCount), 0, &size);

    attributes_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::operator new(size, std::nothrow));
    if (attributes_ == nullptr)
    {
        return wsh::common::Error{"Failed to allocate startup attribute list"};
    }

    if (!InitializeProcThreadAttributeList(attributes_, static_cast<DWORD>(attributeCount), 0, &size))
    {
        const auto message = GetLastErrorMessage("InitializeProcThreadAttributeList failed");
        ::operator delete(attributes_);
        attributes_ = nullptr;
        return wsh::common::Error{message};
    }

    return {};
}

wsh::common::Result<ProcessHandles> LaunchProcess(
    const std::wstring& commandLine,
    STARTUPINFOEXW& startupInfo,
    bool inheritHandles,
    const std::wstring& workingDirectory,
    const std::vector<wchar_t>* environmentBlock)
{
    std::wstring mutableCommandLine = commandLine;

    PROCESS_INFORMATION processInformation{};
    const DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT |
        ((environmentBlock != nullptr) ? CREATE_UNICODE_ENVIRONMENT : 0);

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        inheritHandles ? TRUE : FALSE,
        creationFlags,
        environmentBlock != nullptr ? const_cast<wchar_t*>(environmentBlock->data()) : nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo.StartupInfo,
        &processInformation);

    if (!created)
    {
        return wsh::common::Error{GetLastErrorMessage("CreateProcessW failed")};
    }

    return ProcessHandles{UniqueHandle{processInformation.hProcess}, UniqueHandle{processInformation.hThread}};
}
} // namespace wsh::platform::windows
