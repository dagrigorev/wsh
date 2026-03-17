#include <cassert>
#include <string>

#include "wsh/common/unicode.h"

void RunUnicodeTests()
{
    const auto* raw = u8"Привет";
    const std::string utf8(reinterpret_cast<const char*>(raw));

    const auto wide = wsh::common::Utf8ToWide(utf8);
    assert(!wide.empty());

    const auto roundtrip = wsh::common::WideToUtf8(wide);
    assert(!roundtrip.empty());
}
