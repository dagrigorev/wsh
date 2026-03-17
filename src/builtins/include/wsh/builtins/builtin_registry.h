#pragma once

#include <string>
#include <unordered_set>

namespace wsh::builtins
{
class BuiltinRegistry
{
public:
    BuiltinRegistry();
    [[nodiscard]] bool Contains(const std::string& name) const;

private:
    std::unordered_set<std::string> builtins_{};
};
} // namespace wsh::builtins
