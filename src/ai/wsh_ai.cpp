#include "wsh_ai.h"
#include "ai_context.h"
#include "ai_suggestion.h"
#include "ai_provider.h"
#include "providers/ngram_ai_provider.h"
#include "../shell/history.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

/* ── Opaque AI state ─────────────────────────────────────────────────────── */

struct WshAiState {
    wsh::NgramAiProvider provider;
    WshAiContext context;
};

/* ── C API — lifecycle ───────────────────────────────────────────────────── */

extern "C" void *wsh_ai_state_create(void) {
    return new (std::nothrow) WshAiState();
}

extern "C" void wsh_ai_state_free(void *state) {
    delete static_cast<WshAiState *>(state);
}

/* ── Context scanning helpers ────────────────────────────────────────────── */

static bool DirExists(const std::string &path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileExists(const std::string &path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool HasGitRepo(const std::string &cwd) {
    std::string dir = cwd;
    while (!dir.empty()) {
        if (DirExists(dir + "\\.git")) return true;
        size_t pos = dir.find_last_of("\\/");
        if (pos == std::string::npos || pos == 0) break;
        dir = dir.substr(0, pos);
    }
    return false;
}

static bool HasGlobPattern(const std::string &dir, const std::string &pattern) {
    std::string search = dir + "\\" + pattern;
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    FindClose(hFind);
    return true;
}

/* ── Context update ──────────────────────────────────────────────────────── */

extern "C" void wsh_ai_update_context(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    auto &c = s->context;

    c.cwd = ctx->cwd;
    c.lastExitCode = ctx->last_status;
    c.lastCommand = ctx->last_command;

    /* History snapshot (last 50) */
    c.recentCommands.clear();
    int hcount = history_count(&ctx->history);
    int start = hcount > 50 ? hcount - 50 : 0;
    for (int i = start; i < hcount; i++) {
        const char *entry = history_at(&ctx->history, i);
        if (entry) c.recentCommands.push_back(entry);
    }

    /* Directory scan */
    c.filesInCurrentDir.clear();
    std::string cwdStr = ctx->cwd;

    c.isGitRepo = HasGitRepo(cwdStr);
    c.hasCMakeLists = FileExists(cwdStr + "\\CMakeLists.txt");
    c.hasPackageJson = FileExists(cwdStr + "\\package.json");
    c.hasSln = HasGlobPattern(cwdStr, "*.sln");
    c.hasCsproj = HasGlobPattern(cwdStr, "*.csproj");
}

/* ── C API — builtin handlers ────────────────────────────────────────────── */

extern "C" void wsh_ai_cmd_status(ShellContext *ctx) {
    io_writeln(ctx->io, ctx->ai_enabled ? "AI: enabled" : "AI: disabled");
    io_writeln(ctx->io, "Provider: ngram+rules");
    io_writeln(ctx->io, "Model: 4-gram (built-in, 74 KB)");
    io_writeln(ctx->io, "Network: disabled");
    io_writeln(ctx->io, "Default: enabled");
}

extern "C" void wsh_ai_cmd_suggest(ShellContext *ctx, int argc, char **argv) {
    if (!ctx || !ctx->ai_state) { io_writeln(ctx->io, "AI: not initialized"); return; }
    auto *s = static_cast<WshAiState *>(ctx->ai_state);

    wsh_ai_update_context(ctx);
    auto &c = s->context;

    /* Determine filter */
    bool filterBuild = false, filterTest = false, filterGit = false;
    for (int i = 0; i < argc; i++) {
        if (argv[i] && _stricmp(argv[i], "build") == 0) filterBuild = true;
        else if (argv[i] && _stricmp(argv[i], "test") == 0) filterTest = true;
        else if (argv[i] && _stricmp(argv[i], "git") == 0) filterGit = true;
    }

    auto suggestions = s->provider.SuggestProjectActions(c);

    bool anyOutput = false;

    if (c.hasCMakeLists && !filterTest && !filterGit) {
        io_writeln(ctx->io, "Detected: CMake project");
        io_writeln(ctx->io, "");
        io_writeln(ctx->io, "Suggested:");
        for (auto &su : suggestions) {
            if (su.title.find("cmake") != std::string::npos) {
                std::string line = "  " + su.command;
                io_writeln(ctx->io, line.c_str());
                anyOutput = true;
            }
        }
        io_writeln(ctx->io, "");
    }

    if ((c.hasSln || c.hasCsproj) && !filterGit) {
        io_writeln(ctx->io, "Detected: .NET project");
        io_writeln(ctx->io, "");
        io_writeln(ctx->io, "Suggested:");
        for (auto &su : suggestions) {
            if (su.title.find("dotnet") != std::string::npos) {
                if (filterBuild && su.title.find("test") != std::string::npos) continue;
                if (filterTest && su.title.find("build") != std::string::npos) continue;
                std::string line = "  " + su.command;
                io_writeln(ctx->io, line.c_str());
                anyOutput = true;
            }
        }
        io_writeln(ctx->io, "");
    }

    if (c.hasPackageJson && !filterGit) {
        io_writeln(ctx->io, "Detected: Node project");
        io_writeln(ctx->io, "");
        io_writeln(ctx->io, "Suggested:");
        for (auto &su : suggestions) {
            if (su.title.find("npm") != std::string::npos) {
                if (filterBuild && su.title.find("test") != std::string::npos) continue;
                if (filterTest && su.title.find("build") != std::string::npos) continue;
                if (filterTest && su.title.find("install") != std::string::npos) continue;
                std::string line = "  " + su.command;
                io_writeln(ctx->io, line.c_str());
                anyOutput = true;
            }
        }
        io_writeln(ctx->io, "");
    }

    if (c.isGitRepo && !filterBuild && !filterTest) {
        io_writeln(ctx->io, "Detected: Git repository");
        io_writeln(ctx->io, "");
        io_writeln(ctx->io, "Suggested:");
        for (auto &su : suggestions) {
            if (su.title.find("git") != std::string::npos) {
                std::string line = "  " + su.command;
                io_writeln(ctx->io, line.c_str());
                anyOutput = true;
            }
        }
        io_writeln(ctx->io, "");
    }

    if (!anyOutput) {
        /* Check if git was specifically requested but not found */
        if (filterGit && !c.isGitRepo) {
            io_writeln(ctx->io, "No Git repository detected in the current directory.");
        } else {
            io_writeln(ctx->io, "No project-specific suggestions available.");
        }
    }
}

extern "C" void wsh_ai_cmd_explain(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) { io_writeln(ctx->io, "AI: not initialized"); return; }
    auto *s = static_cast<WshAiState *>(ctx->ai_state);

    wsh_ai_update_context(ctx);
    AiSuggestion result = s->provider.ExplainLastError(s->context);

    if (!result.text.empty()) {
        io_writeln(ctx->io, result.text.c_str());
    }
}

extern "C" void wsh_ai_cmd_fix(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) { io_writeln(ctx->io, "AI: not initialized"); return; }
    auto *s = static_cast<WshAiState *>(ctx->ai_state);

    wsh_ai_update_context(ctx);
    AiSuggestion result = s->provider.Suggest(s->context);

    if (result.kind == AiSuggestionKind::FixCommand && !result.text.empty()) {
        io_writeln(ctx->io, result.text.c_str());
    } else {
        io_writeln(ctx->io, "No command fix suggestion available.");
    }
}
