#include "wsh/parser/parser.h"

namespace wsh::parser
{
ParsedCommand Parser::Parse(const std::vector<Token>& tokens) const
{
    ParsedCommand command{};
    if (tokens.empty())
    {
        return command;
    }

    command.name = tokens.front().text;
    for (std::size_t index = 1; index < tokens.size(); ++index)
    {
        command.arguments.push_back(tokens[index].text);
    }
    return command;
}
} // namespace wsh::parser
