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

#ifdef __cplusplus
}
#endif

#endif /* WSH_AI_H */
