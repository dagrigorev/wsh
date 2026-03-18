#pragma once

#include <mutex>
#include <string>

namespace wsh::core
{
    enum class LogLevel
    {
        Info,
        Warning,
        Error
    };

    class Logger
    {
    public:
        static Logger& Instance();

        void Write(LogLevel level, const std::wstring& message);
        void Info(const std::wstring& message);
        void Warning(const std::wstring& message);
        void Error(const std::wstring& message);

    private:
        Logger() = default;
        std::mutex mutex_;
    };
}
