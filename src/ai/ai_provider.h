#pragma once
#ifndef WSH_AI_PROVIDER_H
#define WSH_AI_PROVIDER_H

#include "ai_context.h"
#include "ai_suggestion.h"
#include <vector>

class IAiProvider {
public:
    virtual ~IAiProvider() = default;

    virtual AiSuggestion Suggest(const WshAiContext &context) = 0;
    virtual AiSuggestion ExplainLastError(const WshAiContext &context) = 0;
    virtual std::vector<AiSuggestion> SuggestProjectActions(const WshAiContext &context) = 0;
};

#endif
