#pragma once

#include "config/settings.h"
#include "terminal/terminal_tab.h"

#include <memory>
#include <vector>

namespace wsh::workspace
{
    class Workspace
    {
    public:
        explicit Workspace(const wsh::config::Settings& settings);

        bool OpenProfile(size_t profileIndex, int columns, int rows);
        void CloseActiveTab();
        void NextTab();
        void PreviousTab();
        bool ActivateTab(size_t index);

        [[nodiscard]] const std::vector<std::unique_ptr<wsh::terminal::TerminalTab>>& Tabs() const noexcept { return tabs_; }
        [[nodiscard]] wsh::terminal::TerminalTab* ActiveTab() noexcept;
        [[nodiscard]] const wsh::terminal::TerminalTab* ActiveTab() const noexcept;
        [[nodiscard]] size_t ActiveIndex() const noexcept { return activeIndex_; }
        [[nodiscard]] const wsh::config::Settings& Settings() const noexcept { return settings_; }

    private:
        const wsh::config::Settings settings_;
        std::vector<std::unique_ptr<wsh::terminal::TerminalTab>> tabs_;
        size_t activeIndex_ = 0;
    };
}
