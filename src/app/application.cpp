#include "app/application.h"
#include "app/main_window.h"

namespace wsh::app
{
    int Application::Run(HINSTANCE instance, int showCommand)
    {
        MainWindow window;
        if (!window.Create(instance, showCommand))
        {
            return -1;
        }

        MSG message{};
        while (::GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }
}
