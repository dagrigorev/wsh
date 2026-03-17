#pragma once

#include <vector>

#include "wsh/parser/parsed_command.h"
#include "wsh/parser/token.h"

namespace wsh::parser
{
class Parser
{
public:
    [[nodiscard]] ParsedCommand Parse(const std::vector<Token>& tokens) const;
};
} // namespace wsh::parser
