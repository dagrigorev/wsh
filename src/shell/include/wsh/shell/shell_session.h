#pragma once

#include "wsh/shell/shell_context.h"

namespace wsh::shell
{
class ShellSession
{
public:
    [[nodiscard]] ShellContext& Context() noexcept { return context_; }
    [[nodiscard]] const ShellContext& Context() const noexcept { return context_; }

private:
    ShellContext context_{};
};
} // namespace wsh::shell
