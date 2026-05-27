#pragma once
#ifndef WSH_PHI4_PROMPT_BUILDER_H
#define WSH_PHI4_PROMPT_BUILDER_H

#include <string>

namespace wsh {

class Phi4PromptBuilder {
public:
    Phi4PromptBuilder();

    /* Set the user's submitted command */
    void SetCommand(const std::string& cmd);

    /* Set the language hint ("ru" or "en") */
    void SetLanguage(const std::string& lang);

    /* Build the full prompt string including system message */
    std::string Build() const;

    /* Get the system prompt only */
    static std::string GetSystemPrompt(const std::string& lang);

    /* Get the maximum allowed response length */
    int GetMaxChars() const { return 120; }

private:
    std::string command_;
    std::string language_;
};

} /* namespace wsh */

#endif /* WSH_PHI4_PROMPT_BUILDER_H */
