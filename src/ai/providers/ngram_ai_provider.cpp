#include "ngram_ai_provider.h"
#include "ngram_model_data.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace wsh {

NgramAiProvider::NgramAiProvider() {
    model_.Init(kModelData, kModelDataSize,
                kModelOrder, kModelNumEntries, kModelTotalCount);
}

std::vector<std::string> NgramAiProvider::GenerateCandidates(
    const std::vector<std::string>& knownCommands,
    const std::string& input) const
{
    std::vector<std::string> candidates;
    std::string firstWord = input;
    size_t space = firstWord.find_first_of(" \t");
    std::string rest;
    if (space != std::string::npos) {
        rest = input.substr(space);
        firstWord = input.substr(0, space);
    }
    if (firstWord.empty()) return candidates;

    for (const auto& known : knownCommands) {
        if (firstWord == known) return candidates;
    }

    for (const auto& known : knownCommands) {
        size_t m = firstWord.size(), n = known.size();
        std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1));
        for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
        for (size_t j = 0; j <= n; ++j) dp[0][j] = j;
        for (size_t i = 1; i <= m; ++i) {
            for (size_t j = 1; j <= n; ++j) {
                size_t cost = (firstWord[i - 1] == known[j - 1]) ? 0 : 1;
                dp[i][j] = std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost});
                if (i > 1 && j > 1 && firstWord[i - 1] == known[j - 2] && firstWord[i - 2] == known[j - 1])
                    dp[i][j] = std::min(dp[i][j], dp[i - 2][j - 2] + 1);
            }
        }
        if (dp[m][n] <= 2)
            candidates.push_back(known + rest);
    }
    return candidates;
}

AiSuggestion NgramAiProvider::RankAndReturn(const std::string& input,
                                              const std::vector<std::string>& candidates) const
{
    AiSuggestion result;
    if (candidates.empty()) return result;

    auto scored = model_.RankCandidates(candidates, input, 2.0);

    result.kind = AiSuggestionKind::FixCommand;
    result.text = "Did you mean:\r\n  " + scored[0].first;
    result.command = scored[0].first;
    result.confidence = 90;
    return result;
}

AiSuggestion NgramAiProvider::Suggest(const WshAiContext& context) {
    if (context.lastCommand.empty()) return {};

    auto candidates = GenerateCandidates(ruleFallback_.GetKnownCommands(),
                                          context.lastCommand);
    if (candidates.empty()) {
        return ruleFallback_.Suggest(context);
    }
    return RankAndReturn(context.lastCommand, candidates);
}

AiSuggestion NgramAiProvider::ExplainLastError(const WshAiContext& context) {
    return ruleFallback_.ExplainLastError(context);
}

std::vector<AiSuggestion> NgramAiProvider::SuggestProjectActions(const WshAiContext& context) {
    return ruleFallback_.SuggestProjectActions(context);
}

}
