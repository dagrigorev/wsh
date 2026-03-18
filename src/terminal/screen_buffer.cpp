#include "terminal/screen_buffer.h"

#include <algorithm>

namespace wsh::terminal
{
    ScreenBuffer::ScreenBuffer(const int columns, const int rows)
        : columns_(std::max(columns, 1)), rows_(std::max(rows, 1))
    {
        for (int i = 0; i < rows_; ++i)
        {
            PushEmptyLine();
        }
        viewportTop_ = std::max(0, static_cast<int>(lines_.size()) - rows_);
    }

    std::vector<Cell> ScreenBuffer::MakeBlankLine() const
    {
        return std::vector<Cell>(columns_, currentStyle_);
    }

    void ScreenBuffer::PushEmptyLine()
    {
        lines_.push_back(MakeBlankLine());
        EnsureHistoryLimit();
    }

    void ScreenBuffer::EnsureHistoryLimit()
    {
        while (static_cast<int>(lines_.size()) > rows_ + maxScrollback_)
        {
            lines_.pop_front();
            if (viewportTop_ > 0)
            {
                --viewportTop_;
            }
        }
    }

    void ScreenBuffer::EnsureCursorInBounds()
    {
        cursor_.row = std::clamp(cursor_.row, 0, std::max(0, static_cast<int>(lines_.size()) - 1));
        cursor_.column = std::clamp(cursor_.column, 0, std::max(0, columns_ - 1));
    }

    void ScreenBuffer::Resize(const int columns, const int rows)
    {
        columns_ = std::max(columns, 1);
        rows_ = std::max(rows, 1);

        for (auto& line : lines_)
        {
            line.resize(columns_, currentStyle_);
        }

        while (static_cast<int>(lines_.size()) < rows_)
        {
            PushEmptyLine();
        }

        EnsureCursorInBounds();
        FollowBottom();
    }

    void ScreenBuffer::PutChar(const wchar_t glyph, const Cell& style)
    {
        EnsureCursorInBounds();
        if (cursor_.row >= static_cast<int>(lines_.size()))
        {
            PushEmptyLine();
        }

        lines_[cursor_.row][cursor_.column] = style;
        lines_[cursor_.row][cursor_.column].glyph = glyph;

        ++cursor_.column;
        if (cursor_.column >= columns_)
        {
            cursor_.column = 0;
            LineFeed();
        }
    }

    void ScreenBuffer::CarriageReturn()
    {
        cursor_.column = 0;
    }

    void ScreenBuffer::LineFeed()
    {
        ++cursor_.row;
        if (cursor_.row >= static_cast<int>(lines_.size()))
        {
            PushEmptyLine();
        }
        FollowBottom();
    }

    void ScreenBuffer::Backspace()
    {
        cursor_.column = std::max(0, cursor_.column - 1);
    }

    void ScreenBuffer::Tab()
    {
        const int next = ((cursor_.column / 4) + 1) * 4;
        cursor_.column = std::min(columns_ - 1, next);
    }

    void ScreenBuffer::MoveCursor(const int row, const int column)
    {
        cursor_.row = std::clamp(row, 0, std::max(0, static_cast<int>(lines_.size()) - 1));
        cursor_.column = std::clamp(column, 0, std::max(0, columns_ - 1));
    }

    void ScreenBuffer::ClearScreen()
    {
        lines_.clear();
        for (int i = 0; i < rows_; ++i)
        {
            PushEmptyLine();
        }
        cursor_ = {};
        FollowBottom();
    }

    void ScreenBuffer::ClearDisplay(const int mode)
    {
        if (mode == 2)
        {
            ClearScreen();
            return;
        }

        const int rowCount = static_cast<int>(lines_.size());
        if (rowCount == 0)
        {
            return;
        }

        if (mode == 0)
        {
            ClearLine(0);
            for (int row = cursor_.row + 1; row < rowCount; ++row)
            {
                for (int column = 0; column < columns_; ++column)
                {
                    lines_[row][column] = currentStyle_;
                    lines_[row][column].glyph = L' ';
                }
            }
        }
        else if (mode == 1)
        {
            for (int row = 0; row < cursor_.row; ++row)
            {
                for (int column = 0; column < columns_; ++column)
                {
                    lines_[row][column] = currentStyle_;
                    lines_[row][column].glyph = L' ';
                }
            }
            ClearLine(1);
        }
    }

    void ScreenBuffer::ClearLine(const int mode)
    {
        auto& line = lines_[cursor_.row];
        const int first = (mode == 1) ? 0 : cursor_.column;
        const int last = (mode == 0) ? (columns_ - 1) : ((mode == 1) ? cursor_.column : (columns_ - 1));
        for (int i = first; i <= last && i < columns_; ++i)
        {
            if (i < 0)
            {
                continue;
            }
            line[i] = currentStyle_;
            line[i].glyph = L' ';
        }
    }

    void ScreenBuffer::ResetAttributes()
    {
        currentStyle_ = {};
    }

    void ScreenBuffer::SetForeground(const D2D1_COLOR_F& color)
    {
        currentStyle_.foreground = color;
    }

    void ScreenBuffer::SetBackground(const D2D1_COLOR_F& color)
    {
        currentStyle_.background = color;
    }

    void ScreenBuffer::ResetForeground()
    {
        currentStyle_.foreground = Cell{}.foreground;
    }

    void ScreenBuffer::ResetBackground()
    {
        currentStyle_.background = Cell{}.background;
    }

    void ScreenBuffer::SetBold(const bool value)
    {
        currentStyle_.bold = value;
    }

    void ScreenBuffer::SetUnderline(const bool value)
    {
        currentStyle_.underline = value;
    }

    void ScreenBuffer::SetInverse(const bool value)
    {
        currentStyle_.inverse = value;
    }

    void ScreenBuffer::SaveCursor()
    {
        savedCursor_ = cursor_;
    }

    void ScreenBuffer::RestoreCursor()
    {
        cursor_ = savedCursor_;
        EnsureCursorInBounds();
    }

    void ScreenBuffer::SetCursorVisible(const bool value)
    {
        cursor_.visible = value;
    }

    void ScreenBuffer::ScrollViewport(const int deltaRows)
    {
        const int maxTop = std::max(0, static_cast<int>(lines_.size()) - rows_);
        viewportTop_ = std::clamp(viewportTop_ + deltaRows, 0, maxTop);
    }

    void ScreenBuffer::FollowBottom()
    {
        viewportTop_ = std::max(0, static_cast<int>(lines_.size()) - rows_);
    }

    std::wstring ScreenBuffer::CopySelection(const SelectionPoint& start, const SelectionPoint& end) const
    {
        const SelectionPoint left = std::min(start, end);
        const SelectionPoint right = std::max(start, end);
        std::wstring result;

        for (int row = left.row; row <= right.row && row < static_cast<int>(lines_.size()); ++row)
        {
            const int firstColumn = (row == left.row) ? left.column : 0;
            const int lastColumn = (row == right.row) ? right.column : (columns_ - 1);

            for (int column = firstColumn; column <= lastColumn && column < columns_; ++column)
            {
                result.push_back(lines_[row][column].glyph);
            }

            while (!result.empty() && (result.back() == L' ' || result.back() == L'\0'))
            {
                result.pop_back();
            }

            if (row != right.row)
            {
                result.append(L"\r\n");
            }
        }

        return result;
    }
}


