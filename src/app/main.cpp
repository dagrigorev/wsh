#include "app/application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wsh::app::Application app;
    return app.Run(instance, showCommand);
}
