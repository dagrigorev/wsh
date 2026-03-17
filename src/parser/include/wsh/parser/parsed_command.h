#pragma once

#include <string>
#include <vector>

namespace wsh::parser
{
struct ParsedCommand
{
    std::string name;
    std::vector<std::string> arguments;
};
} // namespace wsh::parser
