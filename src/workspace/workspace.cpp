#include "workspace/workspace.h"

namespace wsh::workspace
{
    Workspace::Workspace(const wsh::config::Settings& settings)
        : settings_(settings)
    {
    }

    bool Workspace::OpenProfile(const size_t profileIndex, const int columns, const int rows)
    {
        if (profileIndex >= settings_.profiles.size())
        {
            return false;
        }

        auto tab = std::make_unique<wsh::terminal::TerminalTab>(settings_.profiles[profileIndex], columns, rows);
        if (!tab->Start())
        {
            return false;
        }

        tabs_.push_back(std::move(tab));
        activeIndex_ = tabs_.size() - 1;
        return true;
    }

    void Workspace::CloseActiveTab()
    {
        CloseTab(activeIndex_);
    }

    bool Workspace::CloseTab(const size_t index)
    {
        if (tabs_.empty() || index >= tabs_.size())
        {
            return false;
        }

        tabs_[index]->Stop();
        tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));

        if (tabs_.empty())
        {
            activeIndex_ = 0;
            return true;
        }

        if (activeIndex_ > index)
        {
            --activeIndex_;
        }
        else if (activeIndex_ >= tabs_.size())
        {
            activeIndex_ = tabs_.size() - 1;
        }

        return true;
    }

    void Workspace::NextTab()
    {
        if (!tabs_.empty())
        {
            activeIndex_ = (activeIndex_ + 1) % tabs_.size();
        }
    }

    void Workspace::PreviousTab()
    {
        if (!tabs_.empty())
        {
            activeIndex_ = (activeIndex_ + tabs_.size() - 1) % tabs_.size();
        }
    }

    bool Workspace::ActivateTab(const size_t index)
    {
        if (index >= tabs_.size())
        {
            return false;
        }

        activeIndex_ = index;
        return true;
    }

    wsh::terminal::TerminalTab* Workspace::ActiveTab() noexcept
    {
        return tabs_.empty() ? nullptr : tabs_[activeIndex_].get();
    }

    const wsh::terminal::TerminalTab* Workspace::ActiveTab() const noexcept
    {
        return tabs_.empty() ? nullptr : tabs_[activeIndex_].get();
    }
}
