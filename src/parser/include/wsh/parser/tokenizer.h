#pragma once

#include <string_view>
#include <vector>

#include "wsh/parser/token.h"

namespace wsh::parser
{
class Tokenizer
{
public:
    [[nodiscard]] std::vector<Token> Tokenize(std::string_view input) const;
};
} // namespace wsh::parser
