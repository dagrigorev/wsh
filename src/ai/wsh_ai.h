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

#ifdef __cplusplus
}
#endif

#endif /* WSH_AI_H */
