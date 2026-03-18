#include "wsh/terminal/screen_buffer.h"

#include "wsh/common/unicode.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace wsh::terminal
{
ScreenBuffer::ScreenBuffer(std::size_t columns, std::size_t rows)
{
    Resize(columns, rows);
}

void ScreenBuffer::Resize(std::size_t columns, std::size_t rows)
{
    columns_ = std::max<std::size_t>(columns, 1);
    rows_ = std::max<std::size_t>(rows, 1);
    cells_.assign(columns_ * rows_, ScreenCell{});
    cursorColumn_ = 0;
    cursorRow_ = 0;
    currentAttributes_ = TextAttributes::Default();
}

std::size_t ScreenBuffer::Index(std::size_t column, std::size_t row) const noexcept
{
    return (row * columns_) + column;
}

void ScreenBuffer::ScrollUp()
{
    if (rows_ <= 1 || columns_ == 0)
    {
        return;
    }

    for (std::size_t row = 1; row < rows_; ++row)
    {
        for (std::size_t col = 0; col < columns_; ++col)
        {
            cells_[Index(col, row - 1)] = cells_[Index(col, row)];
        }
    }

    for (std::size_t col = 0; col < columns_; ++col)
    {
        cells_[Index(col, rows_ - 1)] = ScreenCell{};
    }
}

void ScreenBuffer::PutCodepoint(char32_t ch)
{
    if (columns_ == 0 || rows_ == 0)
    {
        return;
    }

    if (ch == U'\r')
    {
        CarriageReturn();
        return;
    }

    if (ch == U'\n')
    {
        LineFeed();
        return;
    }

    if (ch == U'\b')
    {
        Backspace();
        return;
    }

    ScreenCell& cell = cells_[Index(cursorColumn_, cursorRow_)];
    cell.codepoint = ch == U'\0' ? U' ' : ch;
    cell.attributes = currentAttributes_;

    ++cursorColumn_;
    if (cursorColumn_ >= columns_)
    {
        cursorColumn_ = 0;
        ++cursorRow_;
        if (cursorRow_ >= rows_)
        {
            ScrollUp();
            cursorRow_ = rows_ - 1;
        }
    }
}

void ScreenBuffer::CarriageReturn()
{
    cursorColumn_ = 0;
}

void ScreenBuffer::NewLine()
{
    CarriageReturn();
    LineFeed();
}

void ScreenBuffer::LineFeed()
{
    ++cursorRow_;
    if (cursorRow_ >= rows_)
    {
        ScrollUp();
        cursorRow_ = rows_ - 1;
    }
}

void ScreenBuffer::Backspace()
{
    if (cursorColumn_ > 0)
    {
        --cursorColumn_;
    }
    else if (cursorRow_ > 0)
    {
        --cursorRow_;
        cursorColumn_ = columns_ > 0 ? columns_ - 1 : 0;
    }

    if (columns_ > 0 && rows_ > 0)
    {
        cells_[Index(cursorColumn_, cursorRow_)] = ScreenCell{};
    }
}

void ScreenBuffer::Clear()
{
    std::fill(cells_.begin(), cells_.end(), ScreenCell{});
    cursorColumn_ = 0;
    cursorRow_ = 0;
}

void ScreenBuffer::MoveCursor(std::size_t column, std::size_t row)
{
    if (columns_ == 0 || rows_ == 0)
    {
        cursorColumn_ = 0;
        cursorRow_ = 0;
        return;
    }

    cursorColumn_ = std::min(column, columns_ - 1);
    cursorRow_ = std::min(row, rows_ - 1);
}

void ScreenBuffer::MoveCursorRelative(int deltaColumns, int deltaRows)
{
    const auto clamp_to_range = [](long long value, std::size_t upperBoundExclusive) -> std::size_t
    {
        if (upperBoundExclusive == 0)
        {
            return 0;
        }

        if (value < 0)
        {
            return 0;
        }

        const auto maxValue = static_cast<long long>(upperBoundExclusive - 1);
        if (value > maxValue)
        {
            return static_cast<std::size_t>(maxValue);
        }

        return static_cast<std::size_t>(value);
    };

    const long long nextColumn = static_cast<long long>(cursorColumn_) + static_cast<long long>(deltaColumns);
    const long long nextRow = static_cast<long long>(cursorRow_) + static_cast<long long>(deltaRows);

    cursorColumn_ = clamp_to_range(nextColumn, columns_);
    cursorRow_ = clamp_to_range(nextRow, rows_);
}

void ScreenBuffer::EraseDisplayFromCursor()
{
    if (columns_ == 0 || rows_ == 0)
    {
        return;
    }

    for (std::size_t row = cursorRow_; row < rows_; ++row)
    {
        const std::size_t startCol = (row == cursorRow_) ? cursorColumn_ : 0;
        for (std::size_t col = startCol; col < columns_; ++col)
        {
            cells_[Index(col, row)] = ScreenCell{};
        }
    }
}

void ScreenBuffer::EraseInLineFromCursor()
{
    if (columns_ == 0 || rows_ == 0)
    {
        return;
    }

    for (std::size_t col = cursorColumn_; col < columns_; ++col)
    {
        cells_[Index(col, cursorRow_)] = ScreenCell{};
    }
}

void ScreenBuffer::SetCurrentAttributes(const TextAttributes& attributes) noexcept
{
    currentAttributes_ = attributes;
}

UsedArea ScreenBuffer::MeasureUsedArea() const noexcept
{
    UsedArea used{0, 0};

    for (std::size_t row = 0; row < rows_; ++row)
    {
        std::size_t rowUsedColumns = 0;
        bool hasContent = false;

        for (std::size_t col = 0; col < columns_; ++col)
        {
            const char32_t cp = cells_[Index(col, row)].codepoint;
            if (cp != U' ' && cp != U'\0')
            {
                hasContent = true;
                rowUsedColumns = col + 1;
            }
        }

        if (hasContent)
        {
            used.rows = row + 1;
            used.columns = std::max(used.columns, rowUsedColumns);
        }
    }

    return used;
}

std::string ScreenBuffer::ToUtf8String() const
{
    const auto used = MeasureUsedArea();
    if (used.rows == 0 || used.columns == 0)
    {
        return {};
    }

    std::string out;

    for (std::size_t row = 0; row < used.rows; ++row)
    {
        std::u32string line;
        line.reserve(used.columns);

        for (std::size_t col = 0; col < used.columns; ++col)
        {
            char32_t cp = cells_[Index(col, row)].codepoint;
            if (cp == U'\0')
            {
                cp = U' ';
            }
            line.push_back(cp);
        }

        while (!line.empty() && (line.back() == U' ' || line.back() == U'\0'))
        {
            line.pop_back();
        }

        out += wsh::common::CodePointsToUtf8(line);

        if (row + 1 < used.rows)
        {
            out += "\r\n";
        }
    }

    return out;
}
} // namespace wsh::terminal
