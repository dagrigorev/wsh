#pragma once

#include <string>
#include <vector>

namespace wsh::terminal
{
class ScrollbackBuffer
{
public:
    void AppendLine(std::string line);
    [[nodiscard]] const std::vector<std::string>& Lines() const noexcept { return lines_; }

private:
    std::vector<std::string> lines_{};
};
} // namespace wsh::terminal
