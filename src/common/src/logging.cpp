#include "wsh/common/logging.h"

#include <iostream>

namespace wsh::common
{
void LogMessage(LogLevel, std::string_view message)
{
    std::cerr << message << '\n';
}
} // namespace wsh::common
