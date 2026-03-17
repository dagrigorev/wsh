#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wsh/terminal/cursor_state.h"
#include "wsh/terminal/terminal_types.h"

namespace wsh::terminal
{
class ScreenBuffer
{
public:
    ScreenBuffer(std::size_t columns = 120, std::size_t rows = 30);

    void Resize(std::size_t columns, std::size_t rows);
    void PutChar(char32_t ch);
    void NewLine();
    void CarriageReturn();
    void Backspace();
    void Clear();

    [[nodiscard]] std::size_t Columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t Rows() const noexcept { return rows_; }
    [[nodiscard]] const CursorState& Cursor() const noexcept { return cursor_; }
    [[nodiscard]] const std::vector<ScreenCell>& Cells() const noexcept { return cells_; }
    [[nodiscard]] std::string SnapshotUtf8(bool trimTrailingSpaces = true) const;

private:
    [[nodiscard]] std::size_t Index(std::size_t row, std::size_t column) const noexcept;
    void ScrollUp();

    std::size_t columns_{};
    std::size_t rows_{};
    CursorState cursor_{};
    std::vector<ScreenCell> cells_{};
};
} // namespace wsh::terminal
