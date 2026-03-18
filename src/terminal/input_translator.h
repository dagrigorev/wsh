#pragma once

#include <Windows.h>

#include <optional>
#include <string>

namespace wsh::terminal
{
    class InputTranslator
    {
    public:
        [[nodiscard]] static std::optional<std::string> TranslateKeyDown(WPARAM key, bool ctrlPressed, bool shiftPressed, bool altPressed);
    };
}
