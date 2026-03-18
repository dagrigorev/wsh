#pragma once

#include "terminal/cell.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace wsh::terminal
{
    struct SelectionPoint
    {
        int row = 0;
        int column = 0;

        auto operator<=>(const SelectionPoint&) const = default;
    };

    class ScreenBuffer
    {
    public:
        ScreenBuffer(int columns, int rows);

        void Resize(int columns, int rows);
        void PutChar(wchar_t glyph, const Cell& style);
        void CarriageReturn();
        void LineFeed();
        void Backspace();
        void Tab();
        void MoveCursor(int row, int column);
        void ClearScreen();
        void ClearDisplay(int mode);
        void ClearLine(int mode);
        void ResetAttributes();
        void SaveCursor();
        void RestoreCursor();
        void SetCursorVisible(bool value);
        void SetForeground(const D2D1_COLOR_F& color);
        void SetBackground(const D2D1_COLOR_F& color);
        void ResetForeground();
        void ResetBackground();
        void SetBold(bool value);
        void SetUnderline(bool value);
        void SetInverse(bool value);
        void ScrollViewport(int deltaRows);
        void FollowBottom();

        [[nodiscard]] int Columns() const noexcept { return columns_; }
        [[nodiscard]] int Rows() const noexcept { return rows_; }
        [[nodiscard]] const Cursor& GetCursor() const noexcept { return cursor_; }
        [[nodiscard]] const std::deque<std::vector<Cell>>& Lines() const noexcept { return lines_; }
        [[nodiscard]] int ViewportTop() const noexcept { return viewportTop_; }
        [[nodiscard]] const Cell& DefaultStyle() const noexcept { return currentStyle_; }

        [[nodiscard]] std::wstring CopySelection(const SelectionPoint& start, const SelectionPoint& end) const;

    private:
        void EnsureHistoryLimit();
        void EnsureCursorInBounds();
        void PushEmptyLine();
        std::vector<Cell> MakeBlankLine() const;

        int columns_ = 0;
        int rows_ = 0;
        int maxScrollback_ = 5000;
        int viewportTop_ = 0;
        Cursor cursor_{};
        Cursor savedCursor_{};
        Cell currentStyle_{};
        std::deque<std::vector<Cell>> lines_;
    };
}
