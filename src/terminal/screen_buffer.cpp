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

    void ScreenBuffer::ClearLineFromCursor()
    {
        auto& line = lines_[cursor_.row];
        for (int i = cursor_.column; i < columns_; ++i)
        {
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
