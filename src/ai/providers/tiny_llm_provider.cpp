#include "tiny_llm_provider.h"
#include <chrono>
#include <algorithm>
#include <cstring>

namespace wsh {

TinyLlmProvider::TinyLlmProvider() {}

TinyLlmProvider::~TinyLlmProvider() {
    analysis_running_.store(false);
    analysis_pending_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool TinyLlmProvider::Init() {
    if (initialized_) return true;
    initialized_ = model_.Init();
    return initialized_;
}

/* ─── IAiProvider implementation ────────────────────────────────────────────── */

AiSuggestion TinyLlmProvider::Suggest(const WshAiContext& context) {
    AiSuggestion result;
    /* Check for typo in last command */
    if (!context.lastCommand.empty()) {
        std::string correction;
        if (model_.IsLikelyTypo(context.lastCommand, &correction)) {
            result.kind = AiSuggestionKind::FixCommand;
            result.title = "Typo detected";
            result.text = correction;
            result.confidence = 85;
        }
    }
    return result;
}

AiSuggestion TinyLlmProvider::ExplainLastError(const WshAiContext& context) {
    AiSuggestion result;
    if (context.lastExitCode != 0 && !context.lastCommand.empty()) {
        result.kind = AiSuggestionKind::ExplainError;
        result.title = "Exit code " + std::to_string(context.lastExitCode);
        result.text = "Command: " + context.lastCommand;
        result.confidence = 50;
    }
    return result;
}

std::vector<AiSuggestion> TinyLlmProvider::SuggestProjectActions(
    const WshAiContext& context)
{
    std::vector<AiSuggestion> suggestions;

    if (context.isGitRepo) {
        AiSuggestion s;
        s.kind = AiSuggestionKind::ProjectAction;
        s.title = "git status";
        s.command = "git status";
        s.confidence = 70;
        suggestions.push_back(s);

        s.title = "git log --oneline";
        s.command = "git log --oneline";
        s.confidence = 65;
        suggestions.push_back(s);
    }

    if (context.hasCMakeLists) {
        AiSuggestion s;
        s.kind = AiSuggestionKind::ProjectAction;
        s.title = "cmake -B build";
        s.command = "cmake -B build";
        s.confidence = 75;
        suggestions.push_back(s);

        s.title = "cmake --build build";
        s.command = "cmake --build build";
        s.confidence = 70;
        suggestions.push_back(s);
    }

    if (context.hasPackageJson) {
        AiSuggestion s;
        s.kind = AiSuggestionKind::ProjectAction;
        s.title = "npm install";
        s.command = "npm install";
        s.confidence = 75;
        suggestions.push_back(s);

        s.title = "npm run build";
        s.command = "npm run build";
        s.confidence = 65;
        suggestions.push_back(s);
    }

    if (context.hasSln || context.hasCsproj) {
        AiSuggestion s;
        s.kind = AiSuggestionKind::ProjectAction;
        s.title = "dotnet build";
        s.command = "dotnet build";
        s.confidence = 75;
        suggestions.push_back(s);

        s.title = "dotnet test";
        s.command = "dotnet test";
        s.confidence = 65;
        suggestions.push_back(s);
    }

    return suggestions;
}

/* ─── Proactive reasoning with async worker thread ──────────────────────────── */

void TinyLlmProvider::TriggerAnalysis(const std::string& input) {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        pending_input_ = input;
    }
    analysis_pending_.store(true);

    if (!analysis_running_.exchange(true)) {
        /* Start worker thread */
        if (worker_.joinable()) worker_.join();
        worker_ = std::thread([this]() { AnalysisThread(); });
    }
}

void TinyLlmProvider::AnalysisThread() {
    while (analysis_running_.load()) {
        analysis_pending_.store(false);

        std::string input;
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            input = pending_input_;
        }

        std::string result;
        if (!input.empty()) {
            result = model_.Analyze(input);
        }

        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            cached_result_ = result;
            /* If new input arrived while we were analyzing, process it */
            if (analysis_pending_.load()) {
                continue;
            }
        }

        analysis_running_.store(false);
        break;
    }
    analysis_running_.store(false);
}

std::string TinyLlmProvider::GetProactiveReasoning(const std::string& currentInput) {
    std::lock_guard<std::mutex> lock(result_mutex_);

    /* If input cleared, clear cached result */
    if (currentInput.empty()) {
        cached_result_.clear();
        pending_input_.clear();
        return "";
    }

    /* If the input has changed since last analysis, the caller should
     * have called TriggerAnalysis() already. Return previously cached result. */
    return cached_result_;
}

}  /* namespace wsh */
