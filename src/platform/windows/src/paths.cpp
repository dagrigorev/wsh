#include "wsh/platform/windows/paths.h"
#include "wsh/platform/windows/environment.h"

namespace wsh::platform::windows
{
std::filesystem::path GetHomeDirectory()
{
    return GetEnvironmentVariableValue(L"USERPROFILE");
}
} // namespace wsh::platform::windows
