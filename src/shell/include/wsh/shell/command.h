#pragma once

#include <string>
#include <vector>

namespace wsh::shell
{
struct Command
{
    std::string name;
    std::vector<std::string> arguments;
};
} // namespace wsh::shell
