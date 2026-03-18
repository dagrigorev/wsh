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

namespace wsh::terminal
{
    class TerminalTab
    {
    public:
        TerminalTab(const wsh::config::Profile& profile, int columns, int rows);
        ~TerminalTab();

        bool Start();
        void Resize(int columns, int rows);
        void SendInput(std::string_view utf8);
        void Scroll(int deltaRows);

        [[nodiscard]] std::wstring TitleSnapshot() const;
        [[nodiscard]] const std::wstring& ProfileName() const noexcept { return profile_.name; }
        void SetTitle(std::wstring title);
        [[nodiscard]] const ScreenBuffer& Buffer() const noexcept { return buffer_; }
        [[nodiscard]] ScreenBuffer& Buffer() noexcept { return buffer_; }
        [[nodiscard]] std::mutex& Mutex() noexcept { return mutex_; }
        [[nodiscard]] bool IsRunning() const noexcept { return running_; }

        void OnOutput(std::string_view bytes);
        void Stop();

    private:
        wsh::config::Profile profile_;
        std::wstring title_;
        ScreenBuffer buffer_;
        VtParser parser_;
        conpty::ConptySession session_;
        std::atomic<bool> running_{ false };
        mutable std::mutex mutex_;

        void SetTitleUnlocked(std::wstring title);
    };
}
