#pragma once

#include <Windows.h>

namespace wsh::app
{
    class Application
    {
    public:
        int Run(HINSTANCE instance, int showCommand);
    };
}
