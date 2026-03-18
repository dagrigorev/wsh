#pragma once

#include "core/noncopyable.h"

#include <Windows.h>
#include <consoleapi3.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>


namespace wsh::conpty
{
    class ConptySession final : public wsh::core::NonCopyable
    {
    public:
        using OutputHandler = std::function<void(std::string_view)>;
        using ExitHandler = std::function<void()>;

        ConptySession();
        ~ConptySession();

        bool Start(int columns,
                   int rows,
                   const std::wstring& command,
                   const std::vector<std::wstring>& arguments,
                   OutputHandler outputHandler,
                   ExitHandler exitHandler);

        void Stop();
        void Resize(int columns, int rows) const;
        void Write(std::string_view utf8Text) const;

    private:
        std::wstring BuildCommandLine(const std::wstring& command, const std::vector<std::wstring>& arguments) const;
        void StartReaderThread();

        HPCON pseudoConsole_ = nullptr;
        HANDLE inputWrite_ = nullptr;
        HANDLE outputRead_ = nullptr;
        HANDLE process_ = nullptr;
        HANDLE thread_ = nullptr;
        std::jthread readerThread_;
        OutputHandler outputHandler_;
        ExitHandler exitHandler_;
    };
}
