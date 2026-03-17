#include "wsh/ui/app_window.h"

namespace wsh::ui
{
wsh::common::Result<void> AppWindow::Create(HINSTANCE, int)
{
    return {};
}

int AppWindow::RunMessageLoop()
{
    return 0;
}
} // namespace wsh::ui
