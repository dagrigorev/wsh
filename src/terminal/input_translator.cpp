#include "terminal/input_translator.h"

namespace wsh::terminal
{
    namespace
    {
        std::string CsiKey(const int base, const bool ctrlPressed, const bool shiftPressed, const bool altPressed, const char final)
        {
            int modifier = 1;
            if (shiftPressed) modifier += 1;
            if (altPressed) modifier += 2;
            if (ctrlPressed) modifier += 4;
            if (modifier == 1)
            {
                return std::string("\x1b[") + std::to_string(base) + final;
            }
            return std::string("\x1b[") + std::to_string(base) + ";" + std::to_string(modifier) + final;
        }

        std::string Ss3OrCsi(const bool ctrlPressed, const bool shiftPressed, const bool altPressed, const char plainFinal, const int modifiedBase)
        {
            if (!ctrlPressed && !shiftPressed && !altPressed)
            {
                return std::string("\x1bO") + plainFinal;
            }
            return CsiKey(modifiedBase, ctrlPressed, shiftPressed, altPressed, plainFinal);
        }
    }

    std::optional<std::string> InputTranslator::TranslateKeyDown(const WPARAM key, const bool ctrlPressed, const bool shiftPressed, const bool altPressed)
    {
        switch (key)
        {
        case VK_RETURN:
            return altPressed ? std::string("\x1b\r") : std::string("\r");
        case VK_BACK:
            return ctrlPressed ? std::string("\x08") : std::string("\x7f");
        case VK_TAB:
            if (shiftPressed && !ctrlPressed && !altPressed)
            {
                return std::string("\x1b[Z");
            }
            return altPressed ? std::string("\x1b\t") : std::string("\t");
        case VK_ESCAPE:
            return std::string("\x1b");
        case VK_UP:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'A', 1);
        case VK_DOWN:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'B', 1);
        case VK_RIGHT:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'C', 1);
        case VK_LEFT:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'D', 1);
        case VK_HOME:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'H', 1);
        case VK_END:
            return Ss3OrCsi(ctrlPressed, shiftPressed, altPressed, 'F', 1);
        case VK_INSERT:
            return CsiKey(2, ctrlPressed, shiftPressed, altPressed, '~');
        case VK_DELETE:
            return CsiKey(3, ctrlPressed, shiftPressed, altPressed, '~');
        case VK_PRIOR:
            return CsiKey(5, ctrlPressed, shiftPressed, altPressed, '~');
        case VK_NEXT:
            return CsiKey(6, ctrlPressed, shiftPressed, altPressed, '~');
        default:
            return std::nullopt;
        }
    }
}
