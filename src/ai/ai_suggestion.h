#pragma once
#ifndef WSH_AI_SUGGESTION_H
#define WSH_AI_SUGGESTION_H

#include <string>
#include <vector>

enum class AiSuggestionKind {
    None,
    Completion,
    FixCommand,
    ExplainError,
    ProjectAction,
    HistoryMatch
};

struct AiSuggestion {
    AiSuggestionKind kind = AiSuggestionKind::None;
    std::string title;
    std::string text;
    std::string command;
    int confidence = 0;
};

#endif
