#pragma once

#include "config/settings.h"
#include "terminal/terminal_tab.h"

#include <memory>
#include <string>
#include <vector>

namespace wsh::workspace
{
    struct WorkspaceGroup
    {
        std::wstring name;
        std::vector<std::unique_ptr<wsh::terminal::TerminalTab>> tabs;
        size_t activeIndex = 0;
    };

    class Workspace
    {
    public:
        explicit Workspace(const wsh::config::Settings& settings);

        bool OpenProfile(size_t profileIndex, int columns, int rows);
        bool OpenProfileInWorkspace(size_t workspaceIndex, size_t profileIndex, int columns, int rows);
        void CloseActiveTab();
        bool CloseTab(size_t index);
        void NextTab();
        void PreviousTab();
        bool ActivateTab(size_t index);

        size_t AddWorkspace(std::wstring name = L"");
        bool ActivateWorkspace(size_t index);
        void NextWorkspace();
        void PreviousWorkspace();

        [[nodiscard]] const std::vector<std::unique_ptr<wsh::terminal::TerminalTab>>& Tabs() const noexcept;
        [[nodiscard]] wsh::terminal::TerminalTab* ActiveTab() noexcept;
        [[nodiscard]] const wsh::terminal::TerminalTab* ActiveTab() const noexcept;
        [[nodiscard]] size_t ActiveIndex() const noexcept;
        [[nodiscard]] const std::vector<WorkspaceGroup>& Workspaces() const noexcept { return workspaces_; }
        [[nodiscard]] size_t ActiveWorkspaceIndex() const noexcept { return activeWorkspaceIndex_; }
        [[nodiscard]] const std::wstring& ActiveWorkspaceName() const noexcept;
        [[nodiscard]] const wsh::config::Settings& Settings() const noexcept { return settings_; }
        [[nodiscard]] std::vector<size_t> FilteredSessionIndices(const std::wstring& query) const;

    private:
        const wsh::config::Settings settings_;
        std::vector<WorkspaceGroup> workspaces_;
        size_t activeWorkspaceIndex_ = 0;

        [[nodiscard]] WorkspaceGroup* ActiveWorkspace() noexcept;
        [[nodiscard]] const WorkspaceGroup* ActiveWorkspace() const noexcept;
    };
}
