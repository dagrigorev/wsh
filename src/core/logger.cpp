#include "core/logger.h"

#include <Windows.h>
#include <sstream>

namespace wsh::core
{
    namespace
    {
        const wchar_t* ToString(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Info: return L"INFO";
            case LogLevel::Warning: return L"WARN";
            case LogLevel::Error: return L"ERROR";
            default: return L"UNK";
            }
        }
    }

    Logger& Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::Write(const LogLevel level, const std::wstring& message)
    {
        std::scoped_lock lock(mutex_);

        std::wstringstream stream;
        stream << L"[WSH][" << ToString(level) << L"] " << message << L"\n";
        ::OutputDebugStringW(stream.str().c_str());
    }

    void Logger::Info(const std::wstring& message)
    {
        Write(LogLevel::Info, message);
    }

    void Logger::Warning(const std::wstring& message)
    {
        Write(LogLevel::Warning, message);
    }

    void Logger::Error(const std::wstring& message)
    {
        Write(LogLevel::Error, message);
    }
}
