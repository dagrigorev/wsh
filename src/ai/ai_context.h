#pragma once
#ifndef WSH_AI_CONTEXT_H
#define WSH_AI_CONTEXT_H

#include <string>
#include <vector>

struct WshAiContext {
    std::string currentInput;
    std::string cwd;
    int lastExitCode = 0;

    std::string lastCommand;
    std::string lastStdErrSnippet;

    std::vector<std::string> recentCommands;
    std::vector<std::string> filesInCurrentDir;
    std::vector<std::string> availableBuiltins;

    bool isGitRepo = false;
    bool hasCMakeLists = false;
    bool hasPackageJson = false;
    bool hasSln = false;
    bool hasCsproj = false;
};

#endif
