#pragma once

#include <string_view>

namespace wsh::common
{
enum class LogLevel
{
    Info,
    Warning,
    Error,
    Debug
};

void LogMessage(LogLevel level, std::string_view message);
} // namespace wsh::common
