#include <cassert>
#include <string>

#include "wsh/common/unicode.h"

void RunUnicodeTests()
{
    const std::string utf8 = u8"Привет";
    const auto wide = wsh::common::Utf8ToWide(utf8);
    assert(!wide.empty());
}
