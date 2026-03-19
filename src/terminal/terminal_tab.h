#pragma once

#include "config/settings.h"
#include "conpty/conpty_session.h"
#include "terminal/screen_buffer.h"
#include "terminal/vt_parser.h"

#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace wsh::terminal
{
    enum class PaneLayoutPreset
    {
        Single,
        TwoColumns,
        TwoRows,
        Grid2x2
    };

    class TerminalTab
    {
    public:
        TerminalTab(const wsh::config::Profile& profile, int columns, int rows);
        ~TerminalTab();

        bool Start();
        void Resize(int columns, int rows);
        void ResizePane(size_t paneIndex, int columns, int rows);
        void SendInput(std::string_view utf8);
        void SendInputToPane(size_t paneIndex, std::string_view utf8);
        void Scroll(int deltaRows);
        void ScrollPane(size_t paneIndex, int deltaRows);

        [[nodiscard]] std::wstring TitleSnapshot() const;
        [[nodiscard]] const std::wstring& ProfileName() const noexcept { return profile_.name; }
        void SetTitle(std::wstring title);
        [[nodiscard]] const ScreenBuffer& Buffer() const noexcept;
        [[nodiscard]] ScreenBuffer& Buffer() noexcept;
        [[nodiscard]] std::mutex& Mutex() noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

        [[nodiscard]] size_t PaneCount() const noexcept;
        [[nodiscard]] size_t ActivePaneIndex() const noexcept { return activePaneIndex_; }
        [[nodiscard]] PaneLayoutPreset LayoutPreset() const noexcept { return layoutPreset_; }
        bool ActivatePane(size_t paneIndex);
        bool EnsurePaneCount(size_t desiredCount);
        bool SetLayoutPreset(PaneLayoutPreset preset);
        bool SplitActivePane(PaneLayoutPreset preset);
        bool SwapPanes(size_t first, size_t second);

        [[nodiscard]] const ScreenBuffer& BufferAt(size_t paneIndex) const;
        [[nodiscard]] ScreenBuffer& BufferAt(size_t paneIndex);
        [[nodiscard]] std::mutex& MutexAt(size_t paneIndex);
        [[nodiscard]] std::wstring PaneTitleSnapshot(size_t paneIndex) const;
        [[nodiscard]] bool PaneIsRunning(size_t paneIndex) const;

        void OnOutput(size_t paneIndex, std::string_view bytes);
        void Stop();

    private:
        struct Pane
        {
            explicit Pane(const wsh::config::Profile& profile, int columns, int rows);

            wsh::config::Profile profile;
            std::wstring title;
            ScreenBuffer buffer;
            VtParser parser;
            conpty::ConptySession session;
            std::atomic<bool> running{ false };
            mutable std::mutex mutex;
            int columns = 0;
            int rows = 0;
        };

        wsh::config::Profile profile_;
        int defaultColumns_ = 0;
        int defaultRows_ = 0;
        std::vector<std::unique_ptr<Pane>> panes_;
        size_t activePaneIndex_ = 0;
        PaneLayoutPreset layoutPreset_ = PaneLayoutPreset::Single;

        [[nodiscard]] Pane* ActivePane() noexcept;
        [[nodiscard]] const Pane* ActivePane() const noexcept;
        [[nodiscard]] Pane* GetPane(size_t paneIndex) noexcept;
        [[nodiscard]] const Pane* GetPane(size_t paneIndex) const noexcept;
        bool StartPane(size_t paneIndex);
        bool CreatePane();
        void SetTitleUnlocked(Pane& pane, std::wstring title);
        static size_t RequiredPaneCount(PaneLayoutPreset preset);
    };
}
