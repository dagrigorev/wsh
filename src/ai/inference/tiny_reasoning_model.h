#pragma once
#ifndef WSH_TINY_REASONING_MODEL_H
#define WSH_TINY_REASONING_MODEL_H

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace wsh {

/* Lightweight reasoning engine for shell command analysis.
 * Uses pattern-matching + edit-distance (no external deps).
 * RAM: ~2MB working set. Designed so a trained transformer can
 * replace the heuristic backend without changing the API. */
class TinyReasoningModel {
public:
    TinyReasoningModel();
    ~TinyReasoningModel();

    bool Init();

    /* Analyze partial command input. Returns suggestion/correction text. */
    std::string Analyze(const std::string& input);

    /* Check if input looks like a typo and suggest correction */
    bool IsLikelyTypo(const std::string& input, std::string* correction);

    /* Suggest full command from partial input */
    std::string SuggestCompletion(const std::string& partial);

    void Cancel() { cancel_.store(true); }
    bool IsLoaded() const { return loaded_; }

private:
    bool loaded_ = false;
    std::atomic<bool> cancel_{false};

    /* Levenshtein distance */
    static int EditDistance(const std::string& a, const std::string& b);
};

}  /* namespace wsh */

#endif /* WSH_TINY_REASONING_MODEL_H */
