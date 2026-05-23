#pragma once
#ifndef WSH_RULE_BASED_AI_PROVIDER_H
#define WSH_RULE_BASED_AI_PROVIDER_H

#include "../ai_provider.h"
#include <string>
#include <vector>

class RuleBasedAiProvider : public IAiProvider {
public:
    RuleBasedAiProvider();

    AiSuggestion Suggest(const WshAiContext &context) override;
    AiSuggestion ExplainLastError(const WshAiContext &context) override;
    std::vector<AiSuggestion> SuggestProjectActions(const WshAiContext &context) override;

    const std::vector<std::string>& GetKnownCommands() const { return knownCommands_; }

private:
    std::vector<std::string> knownCommands_;

    static size_t Levenshtein(const std::string &a, const std::string &b);
    std::string FixTypo(const std::string &cmd) const;
    void InitKnownCommands();
};

#endif
