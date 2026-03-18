#include "terminal/input_translator.h"

namespace wsh::terminal
{
    std::optional<std::string> InputTranslator::TranslateKeyDown(const WPARAM key,
                                                                const bool ctrlPressed,
                                                                const bool shiftPressed,
                                                                const bool altPressed)
    {
        if (altPressed)
        {
            return std::nullopt;
        }

        switch (key)
        {
        case VK_RETURN:
            return std::string("\r");
        case VK_BACK:
            return std::string("\x7f");
        case VK_TAB:
            return shiftPressed ? std::string("\x1b[Z") : std::string("\t");
        case VK_ESCAPE:
            return std::string("\x1b");
        case VK_LEFT:
            return ctrlPressed ? std::string("\x1b[1;5D") : std::string("\x1b[D");
        case VK_RIGHT:
            return ctrlPressed ? std::string("\x1b[1;5C") : std::string("\x1b[C");
        case VK_UP:
            return std::string("\x1b[A");
        case VK_DOWN:
            return std::string("\x1b[B");
        case VK_HOME:
            return std::string("\x1b[H");
        case VK_END:
            return std::string("\x1b[F");
        case VK_DELETE:
            return std::string("\x1b[3~");
        case VK_INSERT:
            if (shiftPressed && !ctrlPressed)
            {
                return std::nullopt;
            }
            return std::string("\x1b[2~");
        default:
            return std::nullopt;
        }
    }
}
