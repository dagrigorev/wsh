#include "workspace/workspace.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace wsh::workspace
{
    namespace
    {
        std::wstring Lower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            return value;
        }
    }

    Workspace::Workspace(const wsh::config::Settings& settings)
        : settings_(settings)
    {
        workspaces_.push_back({ L"Workspace", {}, 0 });
    }

    WorkspaceGroup* Workspace::ActiveWorkspace() noexcept
    {
        return workspaces_.empty() ? nullptr : &workspaces_[activeWorkspaceIndex_];
    }

    const WorkspaceGroup* Workspace::ActiveWorkspace() const noexcept
    {
        return workspaces_.empty() ? nullptr : &workspaces_[activeWorkspaceIndex_];
    }

    const std::vector<std::unique_ptr<wsh::terminal::TerminalTab>>& Workspace::Tabs() const noexcept
    {
        static const std::vector<std::unique_ptr<wsh::terminal::TerminalTab>> empty;
        if (const auto* ws = ActiveWorkspace())
        {
            return ws->tabs;
        }
        return empty;
    }

    size_t Workspace::ActiveIndex() const noexcept
    {
        if (const auto* ws = ActiveWorkspace())
        {
            return ws->activeIndex;
        }
        return 0;
    }

    const std::wstring& Workspace::ActiveWorkspaceName() const noexcept
    {
        static const std::wstring fallback = L"Workspace";
        if (const auto* ws = ActiveWorkspace())
        {
            return ws->name;
        }
        return fallback;
    }

    bool Workspace::OpenProfile(const size_t profileIndex, const int columns, const int rows)
    {
        return OpenProfileInWorkspace(activeWorkspaceIndex_, profileIndex, columns, rows);
    }

    bool Workspace::OpenProfileInWorkspace(const size_t workspaceIndex, const size_t profileIndex, const int columns, const int rows)
    {
        if (workspaceIndex >= workspaces_.size() || profileIndex >= settings_.profiles.size())
        {
            return false;
        }

        auto tab = std::make_unique<wsh::terminal::TerminalTab>(settings_.profiles[profileIndex], columns, rows);
        if (!tab->Start())
        {
            return false;
        }

        auto& ws = workspaces_[workspaceIndex];
        ws.tabs.push_back(std::move(tab));
        ws.activeIndex = ws.tabs.size() - 1;
        activeWorkspaceIndex_ = workspaceIndex;
        return true;
    }

    void Workspace::CloseActiveTab()
    {
        CloseTab(ActiveIndex());
    }

    bool Workspace::CloseTab(const size_t index)
    {
        auto* ws = ActiveWorkspace();
        if (ws == nullptr || ws->tabs.empty() || index >= ws->tabs.size())
        {
            return false;
        }

        ws->tabs[index]->Stop();
        ws->tabs.erase(ws->tabs.begin() + static_cast<std::ptrdiff_t>(index));

        if (ws->tabs.empty())
        {
            ws->activeIndex = 0;
            return true;
        }

        if (ws->activeIndex > index)
        {
            --ws->activeIndex;
        }
        else if (ws->activeIndex >= ws->tabs.size())
        {
            ws->activeIndex = ws->tabs.size() - 1;
        }

        return true;
    }

    void Workspace::NextTab()
    {
        auto* ws = ActiveWorkspace();
        if (ws != nullptr && !ws->tabs.empty())
        {
            ws->activeIndex = (ws->activeIndex + 1) % ws->tabs.size();
        }
    }

    void Workspace::PreviousTab()
    {
        auto* ws = ActiveWorkspace();
        if (ws != nullptr && !ws->tabs.empty())
        {
            ws->activeIndex = (ws->activeIndex + ws->tabs.size() - 1) % ws->tabs.size();
        }
    }

    bool Workspace::ActivateTab(const size_t index)
    {
        auto* ws = ActiveWorkspace();
        if (ws == nullptr || index >= ws->tabs.size())
        {
            return false;
        }

        ws->activeIndex = index;
        return true;
    }

    size_t Workspace::AddWorkspace(std::wstring name)
    {
        if (name.empty())
        {
            name = workspaces_.empty() ? L"Workspace" : std::format(L"Workspace #{}", workspaces_.size() + 1);
        }
        workspaces_.push_back({ std::move(name), {}, 0 });
        activeWorkspaceIndex_ = workspaces_.size() - 1;
        return activeWorkspaceIndex_;
    }

    bool Workspace::ActivateWorkspace(const size_t index)
    {
        if (index >= workspaces_.size())
        {
            return false;
        }
        activeWorkspaceIndex_ = index;
        return true;
    }

    void Workspace::NextWorkspace()
    {
        if (!workspaces_.empty())
        {
            activeWorkspaceIndex_ = (activeWorkspaceIndex_ + 1) % workspaces_.size();
        }
    }

    void Workspace::PreviousWorkspace()
    {
        if (!workspaces_.empty())
        {
            activeWorkspaceIndex_ = (activeWorkspaceIndex_ + workspaces_.size() - 1) % workspaces_.size();
        }
    }

    wsh::terminal::TerminalTab* Workspace::ActiveTab() noexcept
    {
        auto* ws = ActiveWorkspace();
        return (ws == nullptr || ws->tabs.empty()) ? nullptr : ws->tabs[ws->activeIndex].get();
    }

    const wsh::terminal::TerminalTab* Workspace::ActiveTab() const noexcept
    {
        const auto* ws = ActiveWorkspace();
        return (ws == nullptr || ws->tabs.empty()) ? nullptr : ws->tabs[ws->activeIndex].get();
    }

    std::vector<size_t> Workspace::FilteredSessionIndices(const std::wstring& query) const
    {
        std::vector<size_t> result;
        const auto* ws = ActiveWorkspace();
        if (ws == nullptr)
        {
            return result;
        }

        const std::wstring q = Lower(query);
        for (size_t i = 0; i < ws->tabs.size(); ++i)
        {
            if (q.empty())
            {
                result.push_back(i);
                continue;
            }
            std::wstring hay = ws->tabs[i]->ProfileName() + L"\n" + ws->tabs[i]->TitleSnapshot();
            hay = Lower(std::move(hay));
            if (hay.find(q) != std::wstring::npos)
            {
                result.push_back(i);
            }
        }
        return result;
    }
}
