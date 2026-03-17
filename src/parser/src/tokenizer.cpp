#include "wsh/parser/tokenizer.h"

#include <sstream>

namespace wsh::parser
{
std::vector<Token> Tokenizer::Tokenize(std::string_view input) const
{
    std::vector<Token> tokens;
    std::istringstream stream(std::string(input));
    std::string item;
    while (stream >> item)
    {
        tokens.push_back({item});
    }
    return tokens;
}
} // namespace wsh::parser
