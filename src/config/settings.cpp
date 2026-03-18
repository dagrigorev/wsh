#include "config/settings.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include "core/utf.h"

namespace wsh::config
{
    namespace
    {
        std::wstring Trim(std::wstring value)
        {
            auto is_space = [](wchar_t ch) { return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n'; };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](wchar_t ch) { return !is_space(ch); }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [&](wchar_t ch) { return !is_space(ch); }).base(), value.end());
            return value;
        }

        std::wstring Unquote(std::wstring value)
        {
            value = Trim(std::move(value));
            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            {
                return value.substr(1, value.size() - 2);
            }
            return value;
        }

        D2D1_COLOR_F ParseColor(const std::wstring& hex)
        {
            std::wstring value = Unquote(hex);
            if (value.size() != 7 || value[0] != L'#')
            {
                return D2D1::ColorF(D2D1::ColorF::White);
            }

            const auto parse_byte = [&](size_t offset) -> float
            {
                const std::wstring part = value.substr(offset, 2);
                const int number = std::stoi(part, nullptr, 16);
                return static_cast<float>(number) / 255.0f;
            };

            return D2D1::ColorF(parse_byte(1), parse_byte(3), parse_byte(5), 1.0f);
        }

        std::vector<std::wstring> ParseArray(const std::wstring& text)
        {
            std::vector<std::wstring> result;
            const size_t open = text.find(L'[');
            const size_t close = text.rfind(L']');
            if (open == std::wstring::npos || close == std::wstring::npos || close <= open)
            {
                return result;
            }

            std::wstring body = text.substr(open + 1, close - open - 1);
            std::wstringstream stream(body);
            std::wstring item;
            while (std::getline(stream, item, L','))
            {
                item = Unquote(item);
                if (!item.empty())
                {
                    result.push_back(item);
                }
            }
            return result;
        }
    }

    Settings LoadSettings(const std::wstring& path)
    {
        Settings settings;
        settings.profiles = {
            { L"PowerShell", L"pwsh.exe", { L"-NoLogo" } },
            { L"CMD", L"cmd.exe", {} },
            { L"WSL", L"wsl.exe", {} }
        };

        std::ifstream file(wsh::core::WideToUtf8(path), std::ios::binary);
        if (!file)
        {
            return settings;
        }

        enum class Section
        {
            Root,
            Theme,
            Profile
        };

        Section section = Section::Root;
        Profile currentProfile{};
        bool hasProfile = false;

        auto flush_profile = [&]()
        {
            if (hasProfile && !currentProfile.name.empty() && !currentProfile.command.empty())
            {
                settings.profiles.push_back(currentProfile);
            }
            currentProfile = {};
            hasProfile = false;
        };

        settings.profiles.clear();

        std::string rawLine;
        while (std::getline(file, rawLine))
        {
            std::wstring line = wsh::core::Utf8ToWide(rawLine);
            line = Trim(line);
            if (line.empty() || line[0] == L'#')
            {
                continue;
            }

            if (line == L"[theme]")
            {
                flush_profile();
                section = Section::Theme;
                continue;
            }

            if (line == L"[[profiles]]")
            {
                flush_profile();
                hasProfile = true;
                section = Section::Profile;
                continue;
            }

            const size_t equals = line.find(L'=');
            if (equals == std::wstring::npos)
            {
                continue;
            }

            const std::wstring key = Trim(line.substr(0, equals));
            const std::wstring value = Trim(line.substr(equals + 1));

            switch (section)
            {
            case Section::Root:
                if (key == L"font_family") settings.fontFamily = Unquote(value);
                else if (key == L"font_size") settings.fontSize = std::stof(value);
                break;
            case Section::Theme:
                if (key == L"background") settings.theme.background = ParseColor(value);
                else if (key == L"foreground") settings.theme.foreground = ParseColor(value);
                else if (key == L"accent") settings.theme.accent = ParseColor(value);
                else if (key == L"muted") settings.theme.muted = ParseColor(value);
                else if (key == L"status_background") settings.theme.statusBackground = ParseColor(value);
                else if (key == L"selection") settings.theme.selection = ParseColor(value);
                break;
            case Section::Profile:
                if (key == L"name") currentProfile.name = Unquote(value);
                else if (key == L"command") currentProfile.command = Unquote(value);
                else if (key == L"arguments") currentProfile.arguments = ParseArray(value);
                break;
            }
        }

        flush_profile();

        if (settings.profiles.empty())
        {
            settings.profiles = {
                { L"PowerShell", L"pwsh.exe", { L"-NoLogo" } },
                { L"CMD", L"cmd.exe", {} },
                { L"WSL", L"wsl.exe", {} }
            };
        }

        return settings;
    }
}
