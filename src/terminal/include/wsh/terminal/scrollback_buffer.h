#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace wsh::terminal
{
class ScrollbackBuffer
{
public:
    explicit ScrollbackBuffer(std::size_t maxLines = 2000) : maxLines_(maxLines) {}

    void AppendLine(std::string line);
    void Clear();

    [[nodiscard]] const std::deque<std::string>& Lines() const noexcept { return lines_; }
    [[nodiscard]] std::size_t MaxLines() const noexcept { return maxLines_; }

private:
    std::size_t maxLines_{2000};
    std::deque<std::string> lines_{};
};
} // namespace wsh::terminal
