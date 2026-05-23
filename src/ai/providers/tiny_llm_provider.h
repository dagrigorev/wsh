#pragma once
#ifndef WSH_TINY_LLM_PROVIDER_H
#define WSH_TINY_LLM_PROVIDER_H

#include "../ai_provider.h"
#include "../ai_context.h"
#include "../ai_suggestion.h"
#include "../inference/tiny_reasoning_model.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

namespace wsh {

/* Proactive reasoning micro-model provider.
 * Runs a tiny inference engine asynchronously on a worker thread.
 * Memory: ~2MB working set, well under 150MB limit.
 * CPU-only, no external dependencies. */
class TinyLlmProvider : public IAiProvider {
public:
    TinyLlmProvider();
    ~TinyLlmProvider() override;

    bool Init();
    bool IsLoaded() const { return initialized_; }

    /* IAiProvider interface */
    AiSuggestion Suggest(const WshAiContext& context) override;
    AiSuggestion ExplainLastError(const WshAiContext& context) override;
    std::vector<AiSuggestion> SuggestProjectActions(const WshAiContext& context) override;

    /* Proactive analysis: call on each keystroke to get live reasoning.
     * Returns the current cached result (updated asynchronously). */
    std::string GetProactiveReasoning(const std::string& currentInput);

    /* Trigger async analysis (called from UI thread, non-blocking) */
    void TriggerAnalysis(const std::string& input);

private:
    TinyReasoningModel model_;
    bool initialized_ = false;

    /* Async analysis state */
    std::mutex result_mutex_;
    std::string cached_result_;
    std::string pending_input_;
    std::atomic<bool> analysis_running_{false};
    std::atomic<bool> analysis_pending_{false};
    std::thread worker_;

    void AnalysisThread();
};

}  /* namespace wsh */

#endif /* WSH_TINY_LLM_PROVIDER_H */
