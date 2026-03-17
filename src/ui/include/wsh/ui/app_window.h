#pragma once

#include <windows.h>

#include "wsh/common/result.h"

namespace wsh::ui
{
class AppWindow
{
public:
    [[nodiscard]] wsh::common::Result<void> Create(HINSTANCE instance, int nCmdShow);
    int RunMessageLoop();

private:
    HWND hwnd_{nullptr};
};
} // namespace wsh::ui
