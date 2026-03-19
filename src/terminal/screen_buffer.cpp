#include "terminal/screen_buffer.h"

#include <algorithm>

namespace wsh::terminal
{
    ScreenBuffer::ScreenBuffer(const int columns, const int rows)
        : columns_(std::max(columns, 1)), rows_(std::max(rows, 1))
    {
        ResetAttributes();
        for (int i = 0; i < rows_; ++i)
        {
            PushEmptyLine();
        }
        viewportTop_ = std::max(0, static_cast<int>(lines_.size()) - rows_);
    }

    std::vector<Cell> ScreenBuffer::MakeBlankLine() const
    {
        return std::vector<Cell>(columns_, defaultStyle_);
    }

    void ScreenBuffer::PushEmptyLine()
    {
        lines_.push_back(MakeBlankLine());
        EnsureHistoryLimit();
    }

    void ScreenBuffer::EnsureHistoryLimit()
    {
        while (!alternateScreenActive_ && static_cast<int>(lines_.size()) > rows_ + maxScrollback_)
        {
            lines_.pop_front();
            if (viewportTop_ > 0)
            {
                --viewportTop_;
            }
            if (cursor_.row > 0)
            {
                --cursor_.row;
            }
            if (savedCursor_.row > 0)
            {
                --savedCursor_.row;
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
            line.resize(columns_, defaultStyle_);
        }
        if (!alternateScreenActive_)
        {
            for (auto& line : primaryLines_)
            {
                line.resize(columns_, defaultStyle_);
            }
        }

        while (static_cast<int>(lines_.size()) < rows_)
        {
            PushEmptyLine();
        }
        EnsureHistoryLimit();
        EnsureCursorInBounds();
        FollowBottom();
    }

    void ScreenBuffer::PutChar(const wchar_t glyph, const Cell& style)
    {
        if (glyph == L'\0')
        {
            return;
        }

        EnsureCursorInBounds();
        if (cursor_.row >= static_cast<int>(lines_.size()))
        {
            PushEmptyLine();
        }

        if (cursor_.column >= columns_)
        {
            cursor_.column = 0;
            ++cursor_.row;
            if (cursor_.row >= static_cast<int>(lines_.size()))
            {
                PushEmptyLine();
            }
        }

        auto& line = lines_.at(cursor_.row);
        line.at(cursor_.column) = style;
        line.at(cursor_.column).glyph = glyph;
        ++cursor_.column;

        if (cursor_.column >= columns_)
        {
            cursor_.column = 0;
            ++cursor_.row;
            if (cursor_.row >= static_cast<int>(lines_.size()))
            {
                PushEmptyLine();
            }
        }

        FollowBottom();
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
        if (cursor_.column > 0)
        {
            --cursor_.column;
        }
    }

    void ScreenBuffer::Tab()
    {
        const int nextStop = ((cursor_.column / 8) + 1) * 8;
        cursor_.column = std::min(nextStop, columns_ - 1);
    }

    void ScreenBuffer::MoveCursor(const int row, const int column)
    {
        cursor_.row = row;
        cursor_.column = column;
        EnsureCursorInBounds();
    }

    void ScreenBuffer::ClearScreen()
    {
        lines_.clear();
        for (int i = 0; i < rows_; ++i)
        {
            PushEmptyLine();
        }
        cursor_ = {};
        viewportTop_ = std::max(0, static_cast<int>(lines_.size()) - rows_);
    }

    void ScreenBuffer::ClearDisplay(const int mode)
    {
        if (lines_.empty())
        {
            return;
        }

        EnsureCursorInBounds();

        switch (mode)
        {
        case 2:
        case 3:
            ClearScreen();
            break;
        case 1:
            for (int row = 0; row < cursor_.row; ++row)
            {
                std::fill(lines_[row].begin(), lines_[row].end(), defaultStyle_);
            }
            for (int col = 0; col <= cursor_.column && col < columns_; ++col)
            {
                lines_[cursor_.row][col] = defaultStyle_;
            }
            break;
        case 0:
        default:
            for (int col = cursor_.column; col < columns_; ++col)
            {
                lines_[cursor_.row][col] = defaultStyle_;
            }
            for (size_t row = static_cast<size_t>(cursor_.row + 1); row < lines_.size(); ++row)
            {
                std::fill(lines_[row].begin(), lines_[row].end(), defaultStyle_);
            }
            break;
        }
    }

    void ScreenBuffer::ClearLine(const int mode)
    {
        if (lines_.empty())
        {
            return;
        }

        EnsureCursorInBounds();
        auto& line = lines_[cursor_.row];
        switch (mode)
        {
        case 1:
            for (int col = 0; col <= cursor_.column && col < columns_; ++col)
            {
                line[col] = defaultStyle_;
            }
            break;
        case 2:
            std::fill(line.begin(), line.end(), defaultStyle_);
            break;
        case 0:
        default:
            for (int col = cursor_.column; col < columns_; ++col)
            {
                line[col] = defaultStyle_;
            }
            break;
        }
    }

    void ScreenBuffer::ResetAttributes()
    {
        defaultStyle_ = {};
        currentStyle_ = defaultStyle_;
    }

    void ScreenBuffer::SaveCursor()
    {
        savedCursor_ = cursor_;
        savedStyle_ = currentStyle_;
    }

    void ScreenBuffer::RestoreCursor()
    {
        cursor_ = savedCursor_;
        currentStyle_ = savedStyle_;
        EnsureCursorInBounds();
    }

    void ScreenBuffer::EnterAlternateScreen()
    {
        if (alternateScreenActive_)
        {
            return;
        }

        primaryLines_ = lines_;
        primaryViewportTop_ = viewportTop_;
        lines_.clear();
        for (int i = 0; i < rows_; ++i)
        {
            lines_.push_back(MakeBlankLine());
        }
        cursor_ = {};
        savedCursor_ = {};
        viewportTop_ = 0;
        alternateScreenActive_ = true;
    }

    void ScreenBuffer::LeaveAlternateScreen()
    {
        if (!alternateScreenActive_)
        {
            return;
        }

        lines_ = std::move(primaryLines_);
        if (lines_.empty())
        {
            for (int i = 0; i < rows_; ++i)
            {
                lines_.push_back(MakeBlankLine());
            }
        }
        viewportTop_ = std::clamp(primaryViewportTop_, 0, std::max(0, static_cast<int>(lines_.size()) - rows_));
        alternateScreenActive_ = false;
        EnsureCursorInBounds();
    }

    void ScreenBuffer::SetCursorVisible(const bool value)
    {
        cursor_.visible = value;
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
        currentStyle_.foreground = defaultStyle_.foreground;
    }

    void ScreenBuffer::ResetBackground()
    {
        currentStyle_.background = defaultStyle_.background;
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

    void ScreenBuffer::SetBracketedPasteMode(const bool value)
    {
        bracketedPasteMode_ = value;
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
        if (lines_.empty())
        {
            return {};
        }

        SelectionPoint first = start;
        SelectionPoint last = end;
        if (last < first)
        {
            std::swap(first, last);
        }

        first.row = std::clamp(first.row, 0, static_cast<int>(lines_.size()) - 1);
        last.row = std::clamp(last.row, 0, static_cast<int>(lines_.size()) - 1);
        first.column = std::clamp(first.column, 0, std::max(0, columns_ - 1));
        last.column = std::clamp(last.column, 0, std::max(0, columns_ - 1));

        std::wstring result;
        for (int row = first.row; row <= last.row; ++row)
        {
            const auto& line = lines_[row];
            const int startColumn = row == first.row ? first.column : 0;
            const int endColumn = row == last.row ? last.column : columns_ - 1;
            int trimmedEnd = std::min(endColumn, columns_ - 1);
            while (trimmedEnd >= startColumn && line[trimmedEnd].glyph == L' ')
            {
                --trimmedEnd;
            }
            for (int column = startColumn; column <= trimmedEnd; ++column)
            {
                result.push_back(line[column].glyph);
            }
            if (row != last.row)
            {
                result += L"\r\n";
            }
        }
        return result;
    }
}
