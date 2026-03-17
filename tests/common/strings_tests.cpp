#include <cassert>

#include "wsh/common/strings.h"

void RunStringsTests()
{
    assert(wsh::common::Trim("  test  ") == "test");
}
