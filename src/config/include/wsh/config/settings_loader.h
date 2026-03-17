#pragma once

#include <filesystem>

#include "wsh/common/result.h"
#include "wsh/config/app_settings.h"

namespace wsh::config
{
class SettingsLoader
{
public:
    [[nodiscard]] wsh::common::Result<AppSettings> LoadFromFile(const std::filesystem::path& path) const;
};
} // namespace wsh::config
