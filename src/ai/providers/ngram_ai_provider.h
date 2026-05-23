#ifndef WSH_NGRAM_AI_PROVIDER_H
#define WSH_NGRAM_AI_PROVIDER_H

#include "../ai_provider.h"
#include "../ai_context.h"
#include "../ai_suggestion.h"
#include "ngram_model.h"
#include "rule_based_ai_provider.h"

namespace wsh {

class NgramAiProvider : public IAiProvider {
public:
    NgramAiProvider();
    ~NgramAiProvider() override = default;

    AiSuggestion Suggest(const WshAiContext& context) override;
    AiSuggestion ExplainLastError(const WshAiContext& context) override;
    std::vector<AiSuggestion> SuggestProjectActions(const WshAiContext& context) override;

    const NgramModel& GetModel() const { return model_; }

private:
    NgramModel model_;
    RuleBasedAiProvider ruleFallback_;

    std::vector<std::string> GenerateCandidates(
        const std::vector<std::string>& knownCommands,
        const std::string& input) const;

    AiSuggestion RankAndReturn(const std::string& input,
                                const std::vector<std::string>& candidates) const;
};

}

#endif
