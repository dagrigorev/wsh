#include "terminal/terminal_tab.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace wsh::terminal
{
    namespace
    {
        std::wstring SanitizeTitle(std::wstring title)
        {
            title.erase(std::remove_if(title.begin(), title.end(), [](const wchar_t ch)
            {
                return (ch < 0x20 && ch != L' ') || ch == 0x7F;
            }), title.end());

            for (auto& ch : title)
            {
                if (ch == L'\r' || ch == L'\n' || ch == L'\t')
                {
                    ch = L' ';
                }
            }

            std::wstring collapsed;
            collapsed.reserve(title.size());
            bool lastSpace = false;
            for (const wchar_t ch : title)
            {
                const bool isSpace = std::iswspace(ch) != 0;
                if (isSpace)
                {
                    if (!lastSpace)
                    {
                        collapsed.push_back(L' ');
                    }
                    lastSpace = true;
                }
                else
                {
                    collapsed.push_back(ch);
                    lastSpace = false;
                }
            }

            while (!collapsed.empty() && collapsed.front() == L' ')
            {
                collapsed.erase(collapsed.begin());
            }
            while (!collapsed.empty() && collapsed.back() == L' ')
            {
                collapsed.pop_back();
            }
            return collapsed;
        }
    }

    TerminalTab::Pane::Pane(const wsh::config::Profile& sourceProfile, const int sourceColumns, const int sourceRows)
        : profile(sourceProfile),
          title(sourceProfile.name),
          buffer(sourceColumns, sourceRows),
          parser(buffer),
          columns(sourceColumns),
          rows(sourceRows)
    {
    }

    TerminalTab::TerminalTab(const wsh::config::Profile& profile, const int columns, const int rows)
        : profile_(profile),
          defaultColumns_(columns),
          defaultRows_(rows)
    {
        CreatePane();
    }

    TerminalTab::~TerminalTab()
    {
        Stop();
    }

    size_t TerminalTab::RequiredPaneCount(const PaneLayoutPreset preset)
    {
        switch (preset)
        {
        case PaneLayoutPreset::Single: return 1;
        case PaneLayoutPreset::TwoColumns: return 2;
        case PaneLayoutPreset::TwoRows: return 2;
        case PaneLayoutPreset::Grid2x2: return 4;
        default: return 1;
        }
    }

    TerminalTab::Pane* TerminalTab::GetPane(const size_t paneIndex) noexcept
    {
        return paneIndex < panes_.size() ? panes_[paneIndex].get() : nullptr;
    }

    const TerminalTab::Pane* TerminalTab::GetPane(const size_t paneIndex) const noexcept
    {
        return paneIndex < panes_.size() ? panes_[paneIndex].get() : nullptr;
    }

    TerminalTab::Pane* TerminalTab::ActivePane() noexcept
    {
        return GetPane(activePaneIndex_);
    }

    const TerminalTab::Pane* TerminalTab::ActivePane() const noexcept
    {
        return GetPane(activePaneIndex_);
    }

    bool TerminalTab::CreatePane()
    {
        const size_t newIndex = panes_.size();
        auto pane = std::make_unique<Pane>(profile_, defaultColumns_, defaultRows_);
        pane->parser.SetWindowTitleHandler([this, newIndex](const std::wstring& title)
        {
            if (Pane* current = GetPane(newIndex))
            {
                std::scoped_lock lock(current->mutex);
                SetTitleUnlocked(*current, title);
            }
        });
        panes_.push_back(std::move(pane));
        return true;
    }

    bool TerminalTab::StartPane(const size_t paneIndex)
    {
        Pane* pane = GetPane(paneIndex);
        if (pane == nullptr || pane->running)
        {
            return pane != nullptr;
        }

        pane->running = pane->session.Start(
            pane->columns,
            pane->rows,
            pane->profile.command,
            pane->profile.arguments,
            [this, paneIndex](const std::string_view bytes) { OnOutput(paneIndex, bytes); },
            [pane]() { pane->running = false; });
        return pane->running;
    }

    bool TerminalTab::Start()
    {
        return StartPane(0);
    }

    bool TerminalTab::EnsurePaneCount(const size_t desiredCount)
    {
        if (desiredCount == 0)
        {
            return false;
        }

        while (panes_.size() < desiredCount)
        {
            const size_t paneIndex = panes_.size();
            CreatePane();
            if (!StartPane(paneIndex))
            {
                return false;
            }
        }

        while (panes_.size() > desiredCount)
        {
            panes_.back()->session.Stop();
            panes_.back()->running = false;
            panes_.pop_back();
        }

        activePaneIndex_ = std::min(activePaneIndex_, panes_.empty() ? size_t{ 0 } : panes_.size() - 1);
        return !panes_.empty();
    }

    bool TerminalTab::SetLayoutPreset(const PaneLayoutPreset preset)
    {
        layoutPreset_ = preset;
        return EnsurePaneCount(RequiredPaneCount(preset));
    }

    bool TerminalTab::SplitActivePane(const PaneLayoutPreset preset)
    {
        layoutPreset_ = preset;
        const size_t previousCount = panes_.size();
        if (!EnsurePaneCount(RequiredPaneCount(preset)))
        {
            return false;
        }
        if (panes_.size() > previousCount)
        {
            activePaneIndex_ = panes_.size() - 1;
        }
        return true;
    }

    bool TerminalTab::SwapPanes(const size_t first, const size_t second)
    {
        return first < panes_.size() && second < panes_.size();
    }

    void TerminalTab::Resize(const int columns, const int rows)
    {
        defaultColumns_ = columns;
        defaultRows_ = rows;
        for (auto& pane : panes_)
        {
            std::scoped_lock lock(pane->mutex);
            pane->columns = columns;
            pane->rows = rows;
            pane->buffer.Resize(columns, rows);
            pane->session.Resize(columns, rows);
        }
    }

    void TerminalTab::ResizePane(const size_t paneIndex, const int columns, const int rows)
    {
        if (Pane* pane = GetPane(paneIndex))
        {
            std::scoped_lock lock(pane->mutex);
            pane->columns = columns;
            pane->rows = rows;
            pane->buffer.Resize(columns, rows);
            pane->session.Resize(columns, rows);
        }
    }

    std::wstring TerminalTab::TitleSnapshot() const
    {
        if (const Pane* pane = ActivePane())
        {
            std::scoped_lock lock(pane->mutex);
            return pane->title;
        }
        return profile_.name;
    }

    std::wstring TerminalTab::PaneTitleSnapshot(const size_t paneIndex) const
    {
        if (const Pane* pane = GetPane(paneIndex))
        {
            std::scoped_lock lock(pane->mutex);
            return pane->title;
        }
        return profile_.name;
    }

    void TerminalTab::SetTitle(std::wstring title)
    {
        if (Pane* pane = ActivePane())
        {
            std::scoped_lock lock(pane->mutex);
            SetTitleUnlocked(*pane, std::move(title));
        }
    }

    void TerminalTab::SetTitleUnlocked(Pane& pane, std::wstring title)
    {
        title = SanitizeTitle(std::move(title));
        if (!title.empty())
        {
            pane.title = std::move(title);
        }
    }

    const ScreenBuffer& TerminalTab::Buffer() const noexcept
    {
        return ActivePane()->buffer;
    }

    ScreenBuffer& TerminalTab::Buffer() noexcept
    {
        return ActivePane()->buffer;
    }

    const ScreenBuffer& TerminalTab::BufferAt(const size_t paneIndex) const
    {
        const Pane* pane = GetPane(paneIndex);
        if (pane == nullptr)
        {
            throw std::out_of_range("paneIndex");
        }
        return pane->buffer;
    }

    ScreenBuffer& TerminalTab::BufferAt(const size_t paneIndex)
    {
        Pane* pane = GetPane(paneIndex);
        if (pane == nullptr)
        {
            throw std::out_of_range("paneIndex");
        }
        return pane->buffer;
    }

    std::mutex& TerminalTab::Mutex() noexcept
    {
        return ActivePane()->mutex;
    }

    std::mutex& TerminalTab::MutexAt(const size_t paneIndex)
    {
        Pane* pane = GetPane(paneIndex);
        if (pane == nullptr)
        {
            throw std::out_of_range("paneIndex");
        }
        return pane->mutex;
    }

    bool TerminalTab::IsRunning() const noexcept
    {
        const Pane* pane = ActivePane();
        return pane != nullptr && pane->running;
    }

    bool TerminalTab::PaneIsRunning(const size_t paneIndex) const
    {
        const Pane* pane = GetPane(paneIndex);
        return pane != nullptr && pane->running;
    }

    size_t TerminalTab::PaneCount() const noexcept
    {
        return panes_.size();
    }

    bool TerminalTab::ActivatePane(const size_t paneIndex)
    {
        if (paneIndex >= panes_.size())
        {
            return false;
        }
        activePaneIndex_ = paneIndex;
        return true;
    }

    void TerminalTab::SendInput(const std::string_view utf8)
    {
        SendInputToPane(activePaneIndex_, utf8);
    }

    void TerminalTab::SendInputToPane(const size_t paneIndex, const std::string_view utf8)
    {
        if (Pane* pane = GetPane(paneIndex))
        {
            {
                std::scoped_lock lock(pane->mutex);
                pane->buffer.FollowBottom();
            }
            pane->session.Write(utf8);
        }
    }

    void TerminalTab::Scroll(const int deltaRows)
    {
        ScrollPane(activePaneIndex_, deltaRows);
    }

    void TerminalTab::ScrollPane(const size_t paneIndex, const int deltaRows)
    {
        if (Pane* pane = GetPane(paneIndex))
        {
            std::scoped_lock lock(pane->mutex);
            pane->buffer.ScrollViewport(deltaRows);
        }
    }

    void TerminalTab::OnOutput(const size_t paneIndex, const std::string_view bytes)
    {
        if (Pane* pane = GetPane(paneIndex))
        {
            std::scoped_lock lock(pane->mutex);
            pane->parser.Process(bytes);
        }
    }

    void TerminalTab::Stop()
    {
        for (auto& pane : panes_)
        {
            pane->running = false;
            pane->session.Stop();
        }
    }
}
