#include "conpty/conpty_session.h"

#include <consoleapi3.h>
#include <processenv.h>
#include <vector>
#include <cstddef>

namespace wsh::conpty
{
    namespace
    {
        void SafeClose(HANDLE& handle)
        {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(handle);
                handle = nullptr;
            }
        }
    }

    ConptySession::ConptySession() = default;

    ConptySession::~ConptySession()
    {
        Stop();
    }

    std::wstring ConptySession::BuildCommandLine(const std::wstring& command, const std::vector<std::wstring>& arguments) const
    {
        std::wstring line = L"\"" + command + L"\"";
        for (const auto& argument : arguments)
        {
            line += L" \"" + argument + L"\"";
        }
        return line;
    }

    bool ConptySession::Start(const int columns,
                              const int rows,
                              const std::wstring& command,
                              const std::vector<std::wstring>& arguments,
                              OutputHandler outputHandler,
                              ExitHandler exitHandler)
    {
        Stop();
        stopping_ = false;

        outputHandler_ = std::move(outputHandler);
        exitHandler_ = std::move(exitHandler);

        HANDLE inputRead = nullptr;
        HANDLE outputWrite = nullptr;
        if (!::CreatePipe(&inputRead, &inputWrite_, nullptr, 0))
        {
            return false;
        }

        if (!::CreatePipe(&outputRead_, &outputWrite, nullptr, 0))
        {
            SafeClose(inputRead);
            SafeClose(inputWrite_);
            return false;
        }

        const HRESULT hr = ::CreatePseudoConsole({ static_cast<short>(columns), static_cast<short>(rows) }, inputRead, outputWrite, 0, &pseudoConsole_);
        SafeClose(inputRead);
        SafeClose(outputWrite);
        if (FAILED(hr))
        {
            Stop();
            return false;
        }

        SIZE_T attributeListSize = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
        auto attributeBuffer = std::make_unique<std::byte[]>(attributeListSize);
        auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.get());
        if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize))
        {
            Stop();
            return false;
        }

        if (!::UpdateProcThreadAttribute(attributeList,
                                         0,
                                         PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                         pseudoConsole_,
                                         sizeof(pseudoConsole_),
                                         nullptr,
                                         nullptr))
        {
            ::DeleteProcThreadAttributeList(attributeList);
            Stop();
            return false;
        }

        STARTUPINFOEXW startupInfo{};
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.lpAttributeList = attributeList;

        PROCESS_INFORMATION processInfo{};
        std::wstring commandLine = BuildCommandLine(command, arguments);
        const BOOL created = ::CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            nullptr,
            &startupInfo.StartupInfo,
            &processInfo);

        ::DeleteProcThreadAttributeList(attributeList);

        if (!created)
        {
            Stop();
            return false;
        }

        process_ = processInfo.hProcess;
        thread_ = processInfo.hThread;
        StartReaderThread();
        return true;
    }

    void ConptySession::StartReaderThread()
    {
        readerThread_ = std::jthread([this](std::stop_token token)
        {
            std::vector<char> buffer(4096);
            while (!token.stop_requested())
            {
                DWORD bytesRead = 0;
                if (outputRead_ == nullptr || !::ReadFile(outputRead_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0)
                {
                    break;
                }

                if (outputHandler_)
                {
                    try
                    {
                        outputHandler_(std::string_view(buffer.data(), bytesRead));
                    }
                    catch (...)
                    {
                        // Keep the hosted shell alive even if the terminal frontend rejected a payload.
                    }
                }
            }

            if (!stopping_ && exitHandler_)
            {
                exitHandler_();
            }
        });
    }

    void ConptySession::Stop()
    {
        stopping_ = true;

        SafeClose(inputWrite_);
        SafeClose(outputRead_);

        if (readerThread_.joinable())
        {
            readerThread_.request_stop();
            readerThread_ = std::jthread{};
        }

        if (pseudoConsole_ != nullptr)
        {
            ::ClosePseudoConsole(pseudoConsole_);
            pseudoConsole_ = nullptr;
        }

        SafeClose(process_);
        SafeClose(thread_);
    }

    void ConptySession::Resize(const int columns, const int rows) const
    {
        if (pseudoConsole_ != nullptr)
        {
            ::ResizePseudoConsole(pseudoConsole_, { static_cast<short>(columns), static_cast<short>(rows) });
        }
    }

    void ConptySession::Write(const std::string_view utf8Text) const
    {
        if (inputWrite_ == nullptr || utf8Text.empty())
        {
            return;
        }

        DWORD written = 0;
        ::WriteFile(inputWrite_, utf8Text.data(), static_cast<DWORD>(utf8Text.size()), &written, nullptr);
    }
}
