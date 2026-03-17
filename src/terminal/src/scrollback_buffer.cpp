#include "wsh/terminal/scrollback_buffer.h"

namespace wsh::terminal
{
void ScrollbackBuffer::AppendLine(std::string line)
{
    lines_.push_back(std::move(line));
}
} // namespace wsh::terminal
