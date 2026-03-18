#include "platform/profile_resolver.h"

#include <Windows.h>

#include <array>

namespace wsh::platform
{
    namespace
    {
        bool LooksLikeExplicitPath(const std::wstring& command)
        {
            return command.find(L'\\') != std::wstring::npos || command.find(L'/') != std::wstring::npos || command.find(L':') != std::wstring::npos;
        }

        std::vector<std::wstring> CandidateExecutables(const wsh::config::Profile& profile)
        {
            if (profile.command == L"pwsh.exe" || profile.command == L"pwsh")
            {
                return { L"pwsh.exe", L"powershell.exe", L"cmd.exe" };
            }

            if (profile.command == L"powershell.exe" || profile.command == L"powershell")
            {
                return { L"powershell.exe", L"pwsh.exe", L"cmd.exe" };
            }

            if (profile.command == L"wsl.exe" || profile.command == L"wsl")
            {
                return { L"wsl.exe", L"cmd.exe" };
            }

            if (profile.command == L"cmd.exe" || profile.command == L"cmd")
            {
                return { L"cmd.exe" };
            }

            return { profile.command };
        }
    }

    std::optional<ResolvedProfile> ProfileResolver::Resolve(const wsh::config::Profile& profile)
    {
        if (LooksLikeExplicitPath(profile.command))
        {
            return ResolvedProfile{ profile.name, profile.command, profile.arguments };
        }

        for (const auto& candidate : CandidateExecutables(profile))
        {
            if (const auto resolvedCommand = ResolveExecutable(candidate))
            {
                auto resolved = ResolvedProfile{ profile.name, *resolvedCommand, profile.arguments };
                if ((candidate == L"cmd.exe") && (profile.command == L"pwsh.exe" || profile.command == L"pwsh" || profile.command == L"powershell.exe" || profile.command == L"powershell"))
                {
                    resolved.name = profile.name + L" (fallback)";
                    resolved.arguments.clear();
                }
                return resolved;
            }
        }

        return std::nullopt;
    }

    std::optional<std::wstring> ProfileResolver::ResolveExecutable(const std::wstring& executableName)
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = ::SearchPathW(nullptr,
                                           executableName.c_str(),
                                           nullptr,
                                           static_cast<DWORD>(buffer.size()),
                                           buffer.data(),
                                           nullptr);
        if (length == 0 || length >= buffer.size())
        {
            return std::nullopt;
        }

        return std::wstring(buffer.data(), length);
    }
}
