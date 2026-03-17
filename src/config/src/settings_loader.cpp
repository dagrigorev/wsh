#include "wsh/config/settings_loader.h"

namespace wsh::config
{
wsh::common::Result<AppSettings> SettingsLoader::LoadFromFile(const std::filesystem::path&) const
{
    AppSettings settings{};
    settings.profiles.push_back({"PowerShell 7", "pwsh.exe"});
    settings.profiles.push_back({"Command Prompt", "cmd.exe"});
    return settings;
}
} // namespace wsh::config
