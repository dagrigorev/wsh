#pragma once

#include "wsh/common/result.h"
#include "wsh/platform/windows/handle.h"

namespace wsh::platform::windows
{
struct PipePair
{
    UniqueHandle read;
    UniqueHandle write;
};

[[nodiscard]] wsh::common::Result<PipePair> CreatePipePair();
} // namespace wsh::platform::windows
