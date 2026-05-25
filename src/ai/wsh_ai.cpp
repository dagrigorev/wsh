#include "wsh_ai.h"
#include "ai_context.h"
#include "ai_suggestion.h"
#include "ai_provider.h"
#include "providers/ngram_ai_provider.h"
#include "providers/tiny_llm_provider.h"
#include "commentary/phi4_commentary_provider.h"
#include "commentary/phi4_config.h"
#include "../shell/history.h"
#include "../platform/config.h"
#include "../core/log.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <mutex>

/* ── Opaque AI state ─────────────────────────────────────────────────────── */

struct WshAiState {
    wsh::NgramAiProvider provider;
    wsh::TinyLlmProvider tiny_llm;
    WshAiContext context;

    /* Proactive reasoning cache (accessed from UI + worker threads) */
    std::mutex reason_mutex;
    std::string reason_result;
    std::string reason_input;

    /* Commentary subsystem */
    wsh::Phi4CommentaryProvider commentary_provider;
    bool commentary_enabled = true;

    /* Commentary text cache for C API */
    std::string commentary_cache;
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
    io_writeln(ctx->io, "Suggestions: ngram+rules+tiny_llm");
    io_writeln(ctx->io, "Model: 4-gram (built-in, 74 KB) + tiny_llm (~2 MB)");
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (s) {
        io_writeln(ctx->io, s->tiny_llm.IsLoaded() ? "Tiny LLM: loaded" : "Tiny LLM: not loaded");
        io_writeln(ctx->io, s->commentary_enabled ? "Commentary: enabled" : "Commentary: disabled");
        io_writeln(ctx->io, s->commentary_provider.IsAvailable()
            ? "Commentary provider: available"
            : "Commentary provider: unavailable");
        const char *provType = s->commentary_provider.GetProviderType();
        if (strcmp(provType, "phi4") == 0) {
            io_writeln(ctx->io, "Commentary model: phi4");
        } else if (strcmp(provType, "fallback") == 0) {
            io_writeln(ctx->io, "Commentary model: fallback");
        } else {
            io_writeln(ctx->io, "Commentary model: none");
        }
        std::string modelPath = s->commentary_provider.GetModelPath();
        if (!modelPath.empty()) {
            std::string pathLine = "Model path: " + modelPath;
            io_writeln(ctx->io, pathLine.c_str());
        }
        io_writeln(ctx->io, s->commentary_provider.IsModelLoaded()
            ? "Phi-4 model: loaded"
            : "Phi-4 model: missing");
    }
    io_writeln(ctx->io, "Network: disabled");
    io_writeln(ctx->io, "Default: enabled");
}

/* ── Proactive reasoning lifecycle ────────────────────────────────────────── */

extern "C" void wsh_ai_init_reasoning(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    s->tiny_llm.Init();
}

extern "C" void wsh_ai_trigger_analysis(ShellContext *ctx, const char *input) {
    if (!ctx || !ctx->ai_state || !input) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (!s->tiny_llm.IsLoaded()) return;

    /* Update context's currentInput for the provider */
    s->context.currentInput = input;

    /* Trigger async analysis */
    s->tiny_llm.TriggerAnalysis(input);
}

extern "C" const char *wsh_ai_get_reasoning(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return "";
    auto *s = static_cast<WshAiState *>(ctx->ai_state);

    /* Get latest proactive reasoning result. This is safe to call
     * from the paint thread - the provider returns cached results. */
    std::string current = s->context.currentInput;
    std::string result = s->tiny_llm.GetProactiveReasoning(current);

    /* Cache in the AI state for C string access */
    {
        std::lock_guard<std::mutex> lock(s->reason_mutex);
        s->reason_result = result;
        s->reason_input = current;
    }

    return s->reason_result.c_str();
}

extern "C" void wsh_ai_clear_reasoning(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    std::lock_guard<std::mutex> lock(s->reason_mutex);
    s->reason_result.clear();
    s->reason_input.clear();
}

extern "C" const char *wsh_ai_get_reasoning_input(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return "";
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    std::lock_guard<std::mutex> lock(s->reason_mutex);
    return s->reason_input.c_str();
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

/* ── Commentary API ─────────────────────────────────────────────────────── */

/* C++ linkage — function declared outside extern "C" in header because it
 * takes struct ConfigAi* (only available from C++ via platform/config.h). */
void wsh_ai_apply_runtime_config(ShellContext *ctx, const ConfigAi *cfg) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (!cfg) return;

    ctx->ai_enabled = cfg->enabled;
    s->commentary_enabled = cfg->commentary.enabled;

    wsh::Phi4Config phi4_cfg;
    phi4_cfg.context_tokens = cfg->phi4.context_tokens;
    phi4_cfg.max_tokens = cfg->phi4.max_tokens;
    phi4_cfg.temperature = cfg->phi4.temperature;

    /* Resolve model path */
    if (cfg->phi4.model_path[0]) {
        phi4_cfg.model_path = cfg->phi4.model_path;
    } else {
        /* Check env var */
        char env_path[1024] = {0};
        if (GetEnvironmentVariableA("WSH_PHI4_MODEL_PATH", env_path, sizeof(env_path)) > 0) {
            phi4_cfg.model_path = env_path;
        } else {
            /* Check relative to executable */
            char exe_dir[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
            char *bs = strrchr(exe_dir, '\\');
            if (bs) {
                *bs = '\0';
                std::string try1 = std::string(exe_dir) + "\\models\\phi-4\\model.gguf";
                DWORD attrs = GetFileAttributesA(try1.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                    phi4_cfg.model_path = try1;
                } else {
                    std::string try2 = std::string(exe_dir) + "\\..\\models\\phi-4\\model.gguf";
                    attrs = GetFileAttributesA(try2.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                        phi4_cfg.model_path = try2;
                    }
                }
            }
        }
    }

    s->commentary_provider.Initialize(phi4_cfg, cfg->commentary.fallback_enabled);
}

extern "C" void wsh_ai_trigger_command_commentary(ShellContext *ctx, const char *command) {
    if (!ctx || !ctx->ai_state || !command) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (!ctx->ai_enabled || !s->commentary_enabled) return;
    s->commentary_provider.QueueCommentary(command);
}

extern "C" const char *wsh_ai_try_get_commentary(ShellContext *ctx, const char *command) {
    if (!ctx || !ctx->ai_state || !command) return "";
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (!ctx->ai_enabled || !s->commentary_enabled) return "";

    s->commentary_cache = s->commentary_provider.TryGetCommentary(command);
    return s->commentary_cache.c_str();
}

extern "C" bool wsh_ai_has_commentary(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return false;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    return ctx->ai_enabled && s->commentary_enabled && s->commentary_provider.IsAvailable();
}

extern "C" void wsh_ai_clear_commentary(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    s->commentary_provider.Clear();
}

extern "C" const char *wsh_ai_commentary_provider_type(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return "unavailable";
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    return s->commentary_provider.GetProviderType();
}

extern "C" bool wsh_ai_commentary_is_enabled(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return false;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    return s->commentary_enabled;
}

extern "C" void wsh_ai_commentary_set_enabled(ShellContext *ctx, bool enabled) {
    if (!ctx || !ctx->ai_state) return;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    s->commentary_enabled = enabled;
}

extern "C" bool wsh_ai_phi4_model_loaded(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return false;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    return s->commentary_provider.IsModelLoaded();
}

extern "C" const char *wsh_ai_phi4_model_path(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return "";
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    s->commentary_cache = s->commentary_provider.GetModelPath();
    return s->commentary_cache.c_str();
}

extern "C" bool wsh_ai_phi4_reload(ShellContext *ctx) {
    if (!ctx || !ctx->ai_state) return false;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    return s->commentary_provider.Reload();
}

extern "C" bool wsh_ai_commentary_test(ShellContext *ctx, const char *command) {
    if (!ctx || !ctx->ai_state || !command) return false;
    auto *s = static_cast<WshAiState *>(ctx->ai_state);
    if (!ctx->ai_enabled || !s->commentary_enabled) return false;

    /* Generate fallback commentary synchronously for test purposes */
    wsh::FallbackCommentaryProvider fallback;
    std::string result = fallback.Generate(command);

    if (!result.empty()) {
        /* Inject directly into the provider cache so wsh_ai_try_get_commentary()
         * finds it on the same call — previously stored only in commentary_cache
         * which wsh_ai_try_get_commentary() never reads. */
        s->commentary_provider.InjectResult(command, result);
        return true;
    }
    return false;
}
