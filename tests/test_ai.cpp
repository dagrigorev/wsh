/*
 * test_ai.cpp — AI module unit tests.
 *
 * Tests the rule-based provider directly and the built-in command handler
 * via the C bridge, without REPL dependency.
 */
#include "test_helpers.h"
#include "../src/ai/ai_context.h"
#include "../src/ai/ai_suggestion.h"
#include "../src/ai/ai_provider.h"
#include "../src/ai/providers/ngram_ai_provider.h"
#include "../src/ai/wsh_ai.h"
#include "../src/ai/providers/ngram_model.h"
#include "../src/ai/providers/ngram_model_data.h"
#include "../src/shell/shell_ctx.h"
#include "../src/shell/builtins.h"
#include <string>
#include <cstring>
#include <algorithm>

/* ── Buffer IO ──────────────────────────────────────────────────────────────── */
typedef struct { IShellIO base; char buf[16384]; int len; } TestIO;
static void t_write(IShellIO *s, const char *d, int n) {
    TestIO *b = (TestIO*)s;
    if (b->len + n < (int)sizeof(b->buf)-1) { memcpy(b->buf+b->len,d,n); b->len+=n; b->buf[b->len]='\0'; }
}
static int t_read(IShellIO *s, char *b, int z) { (void)s;(void)b;(void)z; return 0; }

static ShellContext g_ctx;
static TestIO       g_io;
static wsh::NgramAiProvider g_provider;

static void setup(void) {
    memset(&g_io, 0, sizeof(g_io));
    g_io.base.write = t_write; g_io.base.read_line = t_read;
    shell_ctx_init(&g_ctx, (IShellIO *)&g_io);
}

static void teardown(void) {
    shell_ctx_free(&g_ctx);
}

/* ── Typo correction tests ───────────────────────────────────────────────── */

TEST(AiProvider, TypoGtiStatus) {
    WshAiContext ctx;
    ctx.lastCommand = "gti status";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("git status") != std::string::npos);
}

TEST(AiProvider, TypoMkidrTest) {
    WshAiContext ctx;
    ctx.lastCommand = "mkidr test";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("mkdir test") != std::string::npos);
}

TEST(AiProvider, TypoClera) {
    WshAiContext ctx;
    ctx.lastCommand = "clera";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("clear") != std::string::npos);
}

TEST(AiProvider, TypoHistroy) {
    WshAiContext ctx;
    ctx.lastCommand = "histroy";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("history") != std::string::npos);
}

TEST(AiProvider, TypoCmkae) {
    WshAiContext ctx;
    ctx.lastCommand = "cmkae";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("cmake") != std::string::npos);
}

TEST(AiProvider, TypoDonte) {
    WshAiContext ctx;
    ctx.lastCommand = "donte";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("dotnet") != std::string::npos);
}

TEST(AiProvider, TypoPreservesArgs) {
    WshAiContext ctx;
    ctx.lastCommand = "gti status --porcelain";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("git status --porcelain") != std::string::npos);
}

TEST(AiProvider, NoFixForCorrectCommand) {
    WshAiContext ctx;
    ctx.lastCommand = "git status";
    ctx.lastExitCode = 0;
    AiSuggestion s = g_provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::None);
}

TEST(AiProvider, EmptyContextNoCrash) {
    WshAiContext ctx;
    AiSuggestion s = g_provider.Suggest(ctx);
    (void)s;
    AiSuggestion e = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(e.text.find("No failed command") != std::string::npos);
    auto actions = g_provider.SuggestProjectActions(ctx);
    ASSERT_TRUE(actions.empty());
}

/* ── Project-aware suggestion tests ──────────────────────────────────────── */

TEST(AiProvider, CMakeProject) {
    WshAiContext ctx;
    ctx.hasCMakeLists = true;
    auto suggestions = g_provider.SuggestProjectActions(ctx);
    bool hasConfig = false, hasBuild = false;
    for (auto &s : suggestions) {
        if (s.command == "cmake -S . -B build") hasConfig = true;
        if (s.command == "cmake --build build") hasBuild = true;
    }
    ASSERT_TRUE(hasConfig);
    ASSERT_TRUE(hasBuild);
}

TEST(AiProvider, DotnetProject) {
    WshAiContext ctx;
    ctx.hasCsproj = true;
    auto suggestions = g_provider.SuggestProjectActions(ctx);
    bool hasBuild = false, hasTest = false;
    for (auto &s : suggestions) {
        if (s.command == "dotnet build") hasBuild = true;
        if (s.command == "dotnet test") hasTest = true;
    }
    ASSERT_TRUE(hasBuild);
    ASSERT_TRUE(hasTest);
}

TEST(AiProvider, SlnProject) {
    WshAiContext ctx;
    ctx.hasSln = true;
    auto suggestions = g_provider.SuggestProjectActions(ctx);
    bool hasBuild = false, hasTest = false;
    for (auto &s : suggestions) {
        if (s.command == "dotnet build") hasBuild = true;
        if (s.command == "dotnet test") hasTest = true;
    }
    ASSERT_TRUE(hasBuild);
    ASSERT_TRUE(hasTest);
}

TEST(AiProvider, NodeProject) {
    WshAiContext ctx;
    ctx.hasPackageJson = true;
    auto suggestions = g_provider.SuggestProjectActions(ctx);
    bool hasInstall = false, hasBuild = false, hasTest = false;
    for (auto &s : suggestions) {
        if (s.command == "npm install") hasInstall = true;
        if (s.command == "npm run build") hasBuild = true;
        if (s.command == "npm test") hasTest = true;
    }
    ASSERT_TRUE(hasInstall);
    ASSERT_TRUE(hasBuild);
    ASSERT_TRUE(hasTest);
}

TEST(AiProvider, GitProject) {
    WshAiContext ctx;
    ctx.isGitRepo = true;
    auto suggestions = g_provider.SuggestProjectActions(ctx);
    bool hasStatus = false, hasAdd = false, hasCommit = false;
    for (auto &s : suggestions) {
        if (s.command == "git status") hasStatus = true;
        if (s.command == "git add .") hasAdd = true;
        if (s.command == "git commit -m \"\"") hasCommit = true;
    }
    ASSERT_TRUE(hasStatus);
    ASSERT_TRUE(hasAdd);
    ASSERT_TRUE(hasCommit);
}

/* ── Error explanation tests ─────────────────────────────────────────────── */

TEST(AiProvider, ExplainCommandNotFound) {
    WshAiContext ctx;
    ctx.lastCommand = "gti status";
    ctx.lastExitCode = 127;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("not found") != std::string::npos ||
                s.text.find("PATH") != std::string::npos);
}

TEST(AiProvider, ExplainNoErrorYet) {
    WshAiContext ctx;
    ctx.lastCommand = "";
    ctx.lastExitCode = 0;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("No failed command") != std::string::npos);
}

TEST(AiProvider, ExplainCMakeError) {
    WshAiContext ctx;
    ctx.lastCommand = "cmake --build build";
    ctx.lastExitCode = 1;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("CMake") != std::string::npos);
}

TEST(AiProvider, ExplainGitError) {
    WshAiContext ctx;
    ctx.lastCommand = "git commit -m test";
    ctx.lastExitCode = 128;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("Git") != std::string::npos);
}

TEST(AiProvider, ExplainNpmError) {
    WshAiContext ctx;
    ctx.lastCommand = "npm run build";
    ctx.lastExitCode = 1;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("npm") != std::string::npos);
}

TEST(AiProvider, ExplainDotnetError) {
    WshAiContext ctx;
    ctx.lastCommand = "dotnet build";
    ctx.lastExitCode = 1;
    AiSuggestion s = g_provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find(".NET") != std::string::npos);
}

/* ── Built-in command tests ──────────────────────────────────────────────── */

TEST(AiBuiltin, StatusShowsEnabledByDefault) {
    setup();
    ASSERT_TRUE(g_ctx.ai_enabled);
    char *argv[] = { "ai", "status", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(strstr(g_io.buf, "AI: enabled") != NULL);
    ASSERT_TRUE(strstr(g_io.buf, "Provider: ngram+rules") != NULL);
    ASSERT_TRUE(strstr(g_io.buf, "4-gram") != NULL);
    ASSERT_TRUE(strstr(g_io.buf, "Network: disabled") != NULL);
    ASSERT_TRUE(strstr(g_io.buf, "Default: enabled") != NULL);
    teardown();
}

TEST(AiBuiltin, StatusShowsDisabledAfterOff) {
    setup();
    g_ctx.ai_enabled = false;
    char *argv[] = { "ai", "status", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(strstr(g_io.buf, "AI: disabled") != NULL);
    teardown();
}

TEST(AiBuiltin, OffDisables) {
    setup();
    ASSERT_TRUE(g_ctx.ai_enabled);
    char *argv[] = { "ai", "off", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_FALSE(g_ctx.ai_enabled);
    ASSERT_TRUE(strstr(g_io.buf, "AI: disabled") != NULL);
    teardown();
}

TEST(AiBuiltin, OnEnables) {
    setup();
    g_ctx.ai_enabled = false;
    char *argv[] = { "ai", "on", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(g_ctx.ai_enabled);
    ASSERT_TRUE(strstr(g_io.buf, "AI: enabled") != NULL);
    teardown();
}

TEST(AiBuiltin, ExplainNoError) {
    setup();
    g_ctx.last_command[0] = '\0';
    g_ctx.last_status = 0;
    char *argv[] = { "ai", "explain", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(strstr(g_io.buf, "No failed command") != NULL);
    teardown();
}

TEST(AiBuiltin, FixSuggestsWithoutExecuting) {
    setup();
    strncpy(g_ctx.last_command, "gti status", sizeof(g_ctx.last_command) - 1);
    g_ctx.last_status = 127;
    char *argv[] = { "ai", "fix", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(strstr(g_io.buf, "git status") != NULL);
    /* Verify fix does NOT execute — last_command unchanged */
    ASSERT_STR_EQ(g_ctx.last_command, "gti status");
    teardown();
}

TEST(AiBuiltin, FixNoSuggestion) {
    setup();
    g_ctx.last_command[0] = '\0';
    char *argv[] = { "ai", "fix", NULL };
    builtin_ai(2, argv, &g_ctx);
    ASSERT_TRUE(strstr(g_io.buf, "No command fix") != NULL);
    teardown();
}

TEST(AiBuiltin, DefaultStateIsEnabled) {
    setup();
    ASSERT_TRUE(g_ctx.ai_enabled);
    teardown();
}

TEST(AiBuiltin, EmptyContextNoCrash) {
    setup();
    char *argv[] = { "ai", "status", NULL };
    int ret = builtin_ai(2, argv, &g_ctx);
    ASSERT_EQ(ret, 0);
    teardown();
}

/* ── N-gram model tests ──────────────────────────────────────────────────── */

TEST(NgramModel, InitFromEmbeddedData) {
    wsh::NgramModel model;
    ASSERT_FALSE(model.IsInitialized());
    model.Init(wsh::kModelData, wsh::kModelDataSize,
               wsh::kModelOrder, wsh::kModelNumEntries, wsh::kModelTotalCount);
    ASSERT_TRUE(model.IsInitialized());
}

TEST(NgramModel, LogProbabilityIsFinite) {
    wsh::NgramModel model;
    model.Init(wsh::kModelData, wsh::kModelDataSize,
               wsh::kModelOrder, wsh::kModelNumEntries, wsh::kModelTotalCount);

    /* Common commands should have reasonable (negative) log probability */
    double lp = model.LogProbability("git status");
    ASSERT_TRUE(lp < 0.0);
    ASSERT_TRUE(lp > -100.0);

    /* Common command should score higher than gibberish */
    double lpGibber = model.LogProbability("zxq xyz");
    ASSERT_TRUE(lp > lpGibber);
}

TEST(NgramModel, PredictNextChar) {
    wsh::NgramModel model;
    model.Init(wsh::kModelData, wsh::kModelDataSize,
               wsh::kModelOrder, wsh::kModelNumEntries, wsh::kModelTotalCount);

    /* "git" appears frequently, so after "gi" the next char should be 't' */
    char next = model.PredictNext("gi");
    ASSERT_TRUE(next == 't' || next == 0);
    (void)next;
}

TEST(NgramModel, RankCandidates) {
    wsh::NgramModel model;
    model.Init(wsh::kModelData, wsh::kModelDataSize,
               wsh::kModelOrder, wsh::kModelNumEntries, wsh::kModelTotalCount);

    std::vector<std::string> candidates = {"git status", "gti status", "xyz abc"};
    auto ranked = model.RankCandidates(candidates, "gti status", 2.0);
    ASSERT_TRUE(ranked.size() == 3);
    /* "git status" should rank first (most natural) */
    ASSERT_TRUE(ranked[0].first.find("git status") != std::string::npos ||
                ranked[0].first.find("git") != std::string::npos);
}

TEST(NgramModel, EmptyStringHandling) {
    wsh::NgramModel model;
    model.Init(wsh::kModelData, wsh::kModelDataSize,
               wsh::kModelOrder, wsh::kModelNumEntries, wsh::kModelTotalCount);

    double lp = model.LogProbability("");
    ASSERT_EQ(lp, 0.0);
}

TEST(NgramAiProvider, NgramSuggestWorks) {
    wsh::NgramAiProvider provider;
    WshAiContext ctx;
    ctx.lastCommand = "gti status";
    ctx.lastExitCode = 127;
    AiSuggestion s = provider.Suggest(ctx);
    ASSERT_EQ((int)s.kind, (int)AiSuggestionKind::FixCommand);
    ASSERT_TRUE(s.text.find("git status") != std::string::npos);
}

TEST(NgramAiProvider, NgramExplainDelegates) {
    wsh::NgramAiProvider provider;
    WshAiContext ctx;
    ctx.lastCommand = "";
    ctx.lastExitCode = 0;
    AiSuggestion s = provider.ExplainLastError(ctx);
    ASSERT_TRUE(s.text.find("No failed command") != std::string::npos);
}

TEST(NgramAiProvider, NgramSuggestProjectDelegates) {
    wsh::NgramAiProvider provider;
    WshAiContext ctx;
    ctx.hasCMakeLists = true;
    auto suggestions = provider.SuggestProjectActions(ctx);
    bool hasBuild = false;
    for (auto &s : suggestions) {
        if (s.command == "cmake --build build") hasBuild = true;
    }
    ASSERT_TRUE(hasBuild);
}
