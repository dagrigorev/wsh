#include "wsh/platform/windows/win_error.h"

#include <windows.h>

#include <string>

namespace wsh::platform::windows
{
std::string GetLastErrorMessage()
{
    return "Win32 error";
}
} // namespace wsh::platform::windows
