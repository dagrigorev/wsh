#include "wsh/builtins/builtin_registry.h"

namespace wsh::builtins
{
BuiltinRegistry::BuiltinRegistry()
    : builtins_{"cd", "pwd", "ls", "clear", "history", "alias", "exit"}
{
}

bool BuiltinRegistry::Contains(const std::string& name) const
{
    return builtins_.contains(name);
}
} // namespace wsh::builtins
