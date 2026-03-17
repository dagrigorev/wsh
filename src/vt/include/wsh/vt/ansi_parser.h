#pragma once

#include <string>
#include <string_view>

namespace wsh::terminal
{
class ScreenBuffer;
}

namespace wsh::vt
{
class AnsiParser
{
public:
    void Process(std::string_view data, wsh::terminal::ScreenBuffer& screen);

private:
    enum class State
    {
        Text,
        Escape,
        Csi
    };

    State state_{State::Text};
    std::string pendingUtf8_{};
};
} // namespace wsh::vt
