#include "wsh/terminal/screen_buffer.h"

#include <algorithm>

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
    if (cursor_.row >= rows_ || cursor_.column >= columns_)
    {
        return;
    }

    cells_[Index(cursor_.row, cursor_.column)].codepoint = ch;
    ++cursor_.column;
    if (cursor_.column >= columns_)
    {
        cursor_.column = 0;
        if (cursor_.row + 1 < rows_)
        {
            ++cursor_.row;
        }
    }
}

void ScreenBuffer::NewLine()
{
    if (cursor_.row + 1 < rows_)
    {
        ++cursor_.row;
    }
}

void ScreenBuffer::CarriageReturn()
{
    cursor_.column = 0;
}

void ScreenBuffer::Clear()
{
    std::fill(cells_.begin(), cells_.end(), ScreenCell{});
    cursor_ = {};
}

std::size_t ScreenBuffer::Index(std::size_t row, std::size_t column) const noexcept
{
    return row * columns_ + column;
}
} // namespace wsh::terminal
