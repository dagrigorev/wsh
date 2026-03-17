#pragma once

#include <string>
#include <unordered_map>

namespace wsh::shell
{
struct ShellContext
{
    std::unordered_map<std::string, std::string> aliases;
};
} // namespace wsh::shell
