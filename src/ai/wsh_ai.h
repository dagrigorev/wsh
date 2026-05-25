#pragma once
#ifndef WSH_AI_H
#define WSH_AI_H

#include "../shell/shell_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AI state lifecycle — called from shell_ctx init/free. */
void *wsh_ai_state_create(void);
void  wsh_ai_state_free(void *state);

/* Update context cache from the current ShellContext state. */
void wsh_ai_update_context(ShellContext *ctx);

/* Built-in command handlers (output via ctx->io). */
void wsh_ai_cmd_status(ShellContext *ctx);
void wsh_ai_cmd_suggest(ShellContext *ctx, int argc, char **argv);
void wsh_ai_cmd_explain(ShellContext *ctx);
void wsh_ai_cmd_fix(ShellContext *ctx);

/* ── Proactive reasoning API ───────────────────────────────────────────────── */
/* Initialize the tiny LLM micro-model (allocates ~2MB). Called on startup. */
void wsh_ai_init_reasoning(ShellContext *ctx);

/* Trigger async analysis of current input. Non-blocking, called from REPL. */
void wsh_ai_trigger_analysis(ShellContext *ctx, const char *input);

/* Clear the current reasoning result (called on Enter/Tab/Ctrl+C to hide suggestions). */
void wsh_ai_clear_reasoning(ShellContext *ctx);

/* Get the latest reasoning result text. Returns "" if no result yet. 
 * The result is cached; this is safe to call from the UI paint thread. */
const char *wsh_ai_get_reasoning(ShellContext *ctx);

/* Get the latest analysis input (to check if reasoning is stale). */
const char *wsh_ai_get_reasoning_input(ShellContext *ctx);

/* ── AI Commentary API (C-only — C++ callers include config.h) ─────────── */
/* Trigger commentary generation for the submitted command. Non-blocking. */
void wsh_ai_trigger_command_commentary(ShellContext *ctx, const char *command);

/* Try to get ready commentary for the given command. Returns empty str if not ready. */
const char *wsh_ai_try_get_commentary(ShellContext *ctx, const char *command);

/* Check if commentary is enabled and available. */
bool wsh_ai_has_commentary(ShellContext *ctx);

/* Clear current commentary (called on new Enter). */
void wsh_ai_clear_commentary(ShellContext *ctx);

/* ── AI Commentary built-in helpers (used from builtins.c) ──────────────── */
/* Returns commentary status text lines (printed line-by-line). */
const char *wsh_ai_commentary_provider_type(ShellContext *ctx);
bool wsh_ai_commentary_is_enabled(ShellContext *ctx);
void wsh_ai_commentary_set_enabled(ShellContext *ctx, bool enabled);
bool wsh_ai_phi4_model_loaded(ShellContext *ctx);
const char *wsh_ai_phi4_model_path(ShellContext *ctx);
bool wsh_ai_phi4_reload(ShellContext *ctx);
bool wsh_ai_commentary_test(ShellContext *ctx, const char *command);

#ifdef __cplusplus
}
#endif

/* ── C++-only API (requires #include "platform/config.h") ────────────────── */
#ifdef __cplusplus
/* Forward declaration from platform/config.h */
struct ConfigAi;
/* Apply runtime config from TOML file to shell AI state. */
void wsh_ai_apply_runtime_config(ShellContext *ctx, const struct ConfigAi *cfg);
#endif

#endif /* WSH_AI_H */
