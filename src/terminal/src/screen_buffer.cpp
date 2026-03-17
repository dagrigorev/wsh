#include "wsh/terminal/screen_buffer.h"

#include <algorithm>
#include <string>
#include <vector>

#include "wsh/common/unicode.h"

namespace wsh::terminal
{
ScreenBuffer::ScreenBuffer(std::size_t columns, std::size_t rows)
    : columns_(columns), rows_(rows), cells_(columns * rows)
{
}

void ScreenBuffer::Resize(std::size_t columns, std::size_t rows)
{
    columns_ = columns;
    rows_ = rows;
    cells_.assign(columns_ * rows_, ScreenCell{});
    cursor_ = {};
}

void ScreenBuffer::PutChar(char32_t ch)
{
    if (rows_ == 0 || columns_ == 0)
    {
        return;
    }

    if (cursor_.row >= rows_)
    {
        ScrollUp();
    }

    if (cursor_.column >= columns_)
    {
        cursor_.column = 0;
        NewLine();
    }

    cells_[Index(cursor_.row, cursor_.column)].codepoint = ch;
    ++cursor_.column;

    if (cursor_.column >= columns_)
    {
        cursor_.column = 0;
        NewLine();
    }
}

void ScreenBuffer::NewLine()
{
    cursor_.column = 0;
    if (cursor_.row + 1 < rows_)
    {
        ++cursor_.row;
        return;
    }

    ScrollUp();
}

void ScreenBuffer::CarriageReturn()
{
    cursor_.column = 0;
}

void ScreenBuffer::Backspace()
{
    if (cursor_.column > 0)
    {
        --cursor_.column;
        cells_[Index(cursor_.row, cursor_.column)].codepoint = U' ';
    }
}

void ScreenBuffer::Clear()
{
    std::fill(cells_.begin(), cells_.end(), ScreenCell{});
    cursor_ = {};
}

std::string ScreenBuffer::SnapshotUtf8(bool trimTrailingSpaces) const
{
    std::string out;

    for (std::size_t row = 0; row < rows_; ++row)
    {
        std::u32string line;
        line.reserve(columns_);

        for (std::size_t col = 0; col < columns_; ++col)
        {
            const auto codepoint = cells_[Index(row, col)].codepoint;
            line.push_back(codepoint == U'\0' ? U' ' : codepoint);
        }

        if (trimTrailingSpaces)
        {
            while (!line.empty() && line.back() == U' ')
            {
                line.pop_back();
            }
        }

        for (char32_t ch : line)
        {
            out += wsh::common::EncodeUtf8CodePoint(ch);
        }
        out += '\n';
    }

    return out;
}

std::size_t ScreenBuffer::Index(std::size_t row, std::size_t column) const noexcept
{
    return row * columns_ + column;
}

void ScreenBuffer::ScrollUp()
{
    if (rows_ == 0 || columns_ == 0)
    {
        return;
    }

    for (std::size_t row = 1; row < rows_; ++row)
    {
        for (std::size_t col = 0; col < columns_; ++col)
        {
            cells_[Index(row - 1, col)] = cells_[Index(row, col)];
        }
    }

    for (std::size_t col = 0; col < columns_; ++col)
    {
        cells_[Index(rows_ - 1, col)] = ScreenCell{};
    }

    cursor_.row = rows_ > 0 ? rows_ - 1 : 0;
    cursor_.column = 0;
}
} // namespace wsh::terminal
