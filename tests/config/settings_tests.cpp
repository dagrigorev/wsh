#include <cassert>

#include "wsh/config/settings_loader.h"

void RunStringsTests();
void RunUnicodeTests();
void RunTokenizerTests();

void RunSettingsTests()
{
    const wsh::config::SettingsLoader loader;
    const auto settings = loader.LoadFromFile({});
    assert(settings.HasValue());
    assert(!settings.Value().profiles.empty());
}

int main()
{
    RunStringsTests();
    RunUnicodeTests();
    RunTokenizerTests();
    RunSettingsTests();
    return 0;
}
