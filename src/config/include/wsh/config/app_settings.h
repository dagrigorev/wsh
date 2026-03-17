#pragma once

#include <vector>

#include "wsh/config/profile_settings.h"

namespace wsh::config
{
struct AppSettings
{
    std::vector<ProfileSettings> profiles;
};
} // namespace wsh::config
