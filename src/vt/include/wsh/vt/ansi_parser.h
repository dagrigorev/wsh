#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "wsh/terminal/screen_buffer.h"
#include "wsh/terminal/terminal_types.h"

namespace wsh::vt
{
class AnsiParser
{
public:
    void Process(std::string_view data, wsh::terminal::ScreenBuffer& screen);

private:
    void HandleCsi(wsh::terminal::ScreenBuffer& screen, char finalByte, const std::vector<int>& params);
    void ApplySgr(wsh::terminal::ScreenBuffer& screen, const std::vector<int>& params);

    static wsh::terminal::NamedColor SgrToNamedColor(int sgrCode);
};
} // namespace wsh::vt
