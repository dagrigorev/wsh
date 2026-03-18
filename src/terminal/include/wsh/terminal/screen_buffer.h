#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wsh/terminal/terminal_types.h"

namespace wsh::terminal
{
class ScreenBuffer
{
public:
    ScreenBuffer() = default;
    ScreenBuffer(std::size_t columns, std::size_t rows);

    void Resize(std::size_t columns, std::size_t rows);

    void PutChar(char32_t ch) { PutCodepoint(ch); }
    void PutCodepoint(char32_t ch);

    void CarriageReturn();
    void NewLine();
    void LineFeed();
    void Backspace();
    void Clear();

    void MoveCursor(std::size_t column, std::size_t row);
    void MoveCursorRelative(int deltaColumns, int deltaRows);

    void EraseDisplayFromCursor();
    void EraseInLineFromCursor();

    void SetCurrentAttributes(const TextAttributes& attributes) noexcept;

    [[nodiscard]] std::size_t Columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t Rows() const noexcept { return rows_; }

    [[nodiscard]] UsedArea MeasureUsedArea() const noexcept;
    [[nodiscard]] std::string ToUtf8String() const;

private:
    [[nodiscard]] std::size_t Index(std::size_t column, std::size_t row) const noexcept;
    void ScrollUp();

private:
    std::size_t columns_{0};
    std::size_t rows_{0};
    std::size_t cursorColumn_{0};
    std::size_t cursorRow_{0};
    TextAttributes currentAttributes_{};
    std::vector<ScreenCell> cells_{};
};
} // namespace wsh::terminal
