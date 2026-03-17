#include <cassert>

#include "wsh/parser/tokenizer.h"

void RunTokenizerTests()
{
    const wsh::parser::Tokenizer tokenizer;
    const auto tokens = tokenizer.Tokenize("echo hello world");
    assert(tokens.size() == 3);
}
