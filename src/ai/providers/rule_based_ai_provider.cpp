#include "rule_based_ai_provider.h"
#include <algorithm>
#include <cstring>
#include <sstream>

RuleBasedAiProvider::RuleBasedAiProvider() {
    InitKnownCommands();
}

void RuleBasedAiProvider::InitKnownCommands() {
    knownCommands_ = {
        "git", "cmake", "dotnet", "npm", "node",
        "powershell", "pwsh",
        "cd", "ls", "dir", "mkdir", "rmdir", "rm", "cp", "mv",
        "cat", "grep", "history", "clear", "exit", "help",
        "tree", "touch", "echo", "printf", "print", "export",
        "unset", "alias", "unalias", "source", "type", "which",
        "whence", "command", "eval", "exec", "set", "setopt",
        "unsetopt", "jobs", "fg", "bg", "kill", "wait", "pwd",
        "open", "clip", "env", "sudo", "man", "at", "atq", "atrm",
        "true", "false", "test", "read", "return", "local",
        "typeset", "declare", "hash", "trap", "ai"
    };
}

size_t RuleBasedAiProvider::Levenshtein(const std::string &a, const std::string &b) {
    size_t m = a.size(), n = b.size();
    /* Use Damerau-Levenshtein (optimal string alignment) to handle
     * transpositions like "donte" → "dotnet" or "histroy" → "history". */
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1));
    for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = j;
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1,
                                 dp[i][j - 1] + 1,
                                 dp[i - 1][j - 1] + cost});
            /* Transposition of adjacent characters */
            if (i > 1 && j > 1 &&
                a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                dp[i][j] = std::min(dp[i][j], dp[i - 2][j - 2] + 1);
            }
        }
    }
    return dp[m][n];
}

std::string RuleBasedAiProvider::FixTypo(const std::string &cmd) const {
    if (cmd.empty()) return "";

    std::string firstWord = cmd;
    size_t space = firstWord.find_first_of(" \t");
    std::string rest;
    if (space != std::string::npos) {
        rest = cmd.substr(space);
        firstWord = cmd.substr(0, space);
    }

    if (firstWord.empty()) return "";

    for (const auto &known : knownCommands_) {
        if (firstWord == known) return "";
    }

    std::string best;
    size_t bestDist = 3;
    for (const auto &known : knownCommands_) {
        size_t dist = Levenshtein(firstWord, known);
        if (dist < bestDist) {
            bestDist = dist;
            best = known;
        }
    }

    if (best.empty() || bestDist > 2) return "";

    return best + rest;
}

AiSuggestion RuleBasedAiProvider::Suggest(const WshAiContext &context) {
    AiSuggestion result;

    std::string fixed = FixTypo(context.lastCommand);
    if (!fixed.empty()) {
        result.kind = AiSuggestionKind::FixCommand;
        result.text = "Did you mean:\r\n  " + fixed;
        result.command = fixed;
        result.confidence = 90;
    }

    return result;
}

AiSuggestion RuleBasedAiProvider::ExplainLastError(const WshAiContext &context) {
    AiSuggestion result;
    result.kind = AiSuggestionKind::ExplainError;

    if (context.lastCommand.empty() && context.lastExitCode == 0) {
        result.text = "No failed command recorded yet.";
        result.confidence = 100;
        return result;
    }

    int code = context.lastExitCode;
    std::string cmd = context.lastCommand;
    std::string lower = cmd;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::string reason;
    std::string suggestion;

    if (code == 127) {
        reason = "Command was not found or executable is missing from PATH.";
        suggestion = "Check command spelling or install the required tool.";
    } else if (code == 126) {
        reason = "Command exists but could not be executed.";
        suggestion = "Check file permissions or if the executable is valid.";
    } else if (code == 1 && (lower.find("cmake") != std::string::npos)) {
        reason = "CMake command failed.";
        if (lower.find("build") != std::string::npos) {
            suggestion = "Did you configure the build directory?\n  cmake -S . -B build";
        } else {
            suggestion = "Check CMakeLists.txt for syntax errors.";
        }
    } else if (code == 128 && (lower.find("git") != std::string::npos)) {
        reason = "Git command failed.";
        suggestion = "Are you in a Git repository?\n  Use 'git init' or check the directory.";
    } else if (code == 1 && (lower.find("npm") != std::string::npos)) {
        reason = "npm command failed.";
        suggestion = "Check package.json or run 'npm install' first.";
    } else if (code == 1 && (lower.find("dotnet") != std::string::npos)) {
        reason = ".NET command failed.";
        suggestion = "Check that a .NET project file exists or run 'dotnet restore'.";
    } else if (code == 1 && (lower.find("not found") != std::string::npos ||
                             lower.find("no such") != std::string::npos)) {
        reason = "Path or file was not found.";
        suggestion = "Check the file path and try again.";
    } else if (code == 1 && (lower.find("denied") != std::string::npos ||
                             lower.find("access") != std::string::npos)) {
        reason = "Access was denied.";
        suggestion = "Try running as Administrator or check file permissions.";
    } else if (code != 0) {
        reason = "Command exited with code " + std::to_string(code) + ".";
        suggestion = "Review the command output above for details.";
    } else {
        result.text = "No failed command recorded yet.";
        result.confidence = 100;
        return result;
    }

    result.text = "Last command failed.\r\n\r\nReason:\r\n  " + reason +
                  "\r\n\r\nSuggestion:\r\n  " + suggestion;
    result.confidence = 80;
    return result;
}

static void AddBuildSuggestion(std::vector<AiSuggestion> &list, const std::string &title,
                                const std::string &cmd) {
    AiSuggestion s;
    s.kind = AiSuggestionKind::ProjectAction;
    s.title = title;
    s.command = cmd;
    s.text = cmd;
    s.confidence = 90;
    list.push_back(s);
}

std::vector<AiSuggestion> RuleBasedAiProvider::SuggestProjectActions(const WshAiContext &context) {
    std::vector<AiSuggestion> result;

    if (context.hasCMakeLists) {
        AddBuildSuggestion(result, "cmake configure", "cmake -S . -B build");
        AddBuildSuggestion(result, "cmake build", "cmake --build build");
    }

    if (context.hasSln || context.hasCsproj) {
        AddBuildSuggestion(result, "dotnet build", "dotnet build");
        AiSuggestion testSugg;
        testSugg.kind = AiSuggestionKind::ProjectAction;
        testSugg.title = "dotnet test";
        testSugg.command = "dotnet test";
        testSugg.text = "dotnet test";
        testSugg.confidence = 90;
        result.push_back(testSugg);
    }

    if (context.hasPackageJson) {
        AddBuildSuggestion(result, "npm install", "npm install");
        AddBuildSuggestion(result, "npm build", "npm run build");
        AiSuggestion testSugg;
        testSugg.kind = AiSuggestionKind::ProjectAction;
        testSugg.title = "npm test";
        testSugg.command = "npm test";
        testSugg.text = "npm test";
        testSugg.confidence = 90;
        result.push_back(testSugg);
    }

    if (context.isGitRepo) {
        AddBuildSuggestion(result, "git status", "git status");
        AddBuildSuggestion(result, "git add", "git add .");
        AddBuildSuggestion(result, "git commit", "git commit -m \"\"");
    }

    return result;
}
