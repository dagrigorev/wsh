#pragma once

#include "config/settings.h"

#include <optional>
#include <string>
#include <vector>

namespace wsh::platform
{
    struct ResolvedProfile
    {
        std::wstring name;
        std::wstring command;
        std::vector<std::wstring> arguments;
    };

    class ProfileResolver
    {
    public:
        [[nodiscard]] static std::optional<ResolvedProfile> Resolve(const wsh::config::Profile& profile);

    private:
        [[nodiscard]] static std::optional<std::wstring> ResolveExecutable(const std::wstring& executableName);
    };
}
