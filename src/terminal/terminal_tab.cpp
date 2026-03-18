#include "terminal/terminal_tab.h"

namespace wsh::terminal
{
    TerminalTab::TerminalTab(const wsh::config::Profile& profile, const int columns, const int rows)
        : profile_(profile),
          title_(profile.name),
          buffer_(columns, rows),
          parser_(buffer_)
    {
        parser_.SetWindowTitleHandler([this](const std::wstring& title)
        {
            SetTitleUnlocked(title);
        });
    }

    TerminalTab::~TerminalTab()
    {
        Stop();
    }

    bool TerminalTab::Start()
    {
        running_ = session_.Start(
            buffer_.Columns(),
            buffer_.Rows(),
            profile_.command,
            profile_.arguments,
            [this](const std::string_view bytes) { OnOutput(bytes); },
            [this]() { running_ = false; });
        return running_;
    }

    void TerminalTab::Resize(const int columns, const int rows)
    {
        std::scoped_lock lock(mutex_);
        buffer_.Resize(columns, rows);
        session_.Resize(columns, rows);
    }

    std::wstring TerminalTab::TitleSnapshot() const
    {
        std::scoped_lock lock(mutex_);
        return title_;
    }

    void TerminalTab::SetTitle(std::wstring title)
    {
        std::scoped_lock lock(mutex_);
        SetTitleUnlocked(std::move(title));
    }

    void TerminalTab::SetTitleUnlocked(std::wstring title)
    {
        if (!title.empty())
        {
            title_ = std::move(title);
        }
    }

    void TerminalTab::SendInput(const std::string_view utf8)
    {
        session_.Write(utf8);
    }

    void TerminalTab::Scroll(const int deltaRows)
    {
        std::scoped_lock lock(mutex_);
        buffer_.ScrollViewport(deltaRows);
    }

    void TerminalTab::OnOutput(const std::string_view bytes)
    {
        std::scoped_lock lock(mutex_);
        parser_.Process(bytes);
    }

    void TerminalTab::Stop()
    {
        running_ = false;
        session_.Stop();
    }
}
