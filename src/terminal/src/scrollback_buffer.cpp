#include "wsh/terminal/scrollback_buffer.h"

namespace wsh::terminal
{
void ScrollbackBuffer::AppendLine(std::string line)
{
    if (maxLines_ == 0)
    {
        return;
    }

    lines_.push_back(std::move(line));
    while (lines_.size() > maxLines_)
    {
        lines_.pop_front();
    }
}

void ScrollbackBuffer::Clear()
{
    lines_.clear();
}
} // namespace wsh::terminal
