# AI Command Commentary

Wsh can generate a short playful one-line reaction to commands you type,
after you press Enter. This is separate from the proactive reasoning
(typo detection / overlay suggestions) that runs while you type.

## Architecture

```
User presses Enter
    │
    ├─► execute_line()  ──►  command runs normally (worker thread)
    │
    └─► wsh_ai_trigger_command_commentary()
                │
                └─► CommentaryQueue (async worker thread)
                        │
                        ├─► Phi-4 model (if available)
                        │       └─► GGUF via llama.cpp (future)
                        │
                        └─► Fallback provider (deterministic rules)
                                └─► Always instant, no model needed
```

### Source layout

```
src/ai/
    wsh_ai.h              — C API: wsh_ai_trigger_command_commentary() etc.
    wsh_ai.cpp            — C API impl, wires into Phi4CommentaryProvider
    commentary/
        phi4_commentary_provider.h/cpp   — Main async provider
        phi4_prompt_builder.h/cpp        — Phi-4 prompt construction
        phi4_runtime.h/cpp               — Runtime abstraction + stub
        phi4_config.h/cpp                — Phi-4 config struct
        fallback_commentary_provider.h/cpp — Deterministic fallback
```

## Difference between suggestions and commentary

| Feature | Suggestions (typing) | Commentary (Enter) |
|---------|---------------------|-------------------|
| When | While typing, every keystroke | After pressing Enter |
| Model | N-gram + TinyLLM (~2 MB) | Phi-4 GGUF or fallback |
| UI | Overlay below current line | Printed as terminal line |
| Blocking | Non-blocking | Non-blocking |
| Persistence | Cleared on Enter | One-shot after exec |

## Phi-4 model path resolution

Runtime resolves the model path in this order:

1. **Config**: `ai.phi4.model_path` in `Wsh.toml`
2. **Environment variable**: `WSH_PHI4_MODEL_PATH`
3. **Relative to binary**: `./models/phi-4/model.gguf`
4. **Relative to binary**: `../models/phi-4/model.gguf`

If no model is found, Wsh does not crash. Commentary silently falls back
to deterministic rules (if `ai.commentary.fallback_enabled = true`), or
is disabled.

## Configuration

All settings go in `Wsh.toml`:

```toml
[ai]
enabled = true

[ai.commentary]
enabled = true
provider = "phi4"
prefix = "[ai]"
max_chars = 120
timeout_ms = 250
fallback_enabled = true
language = "ru"

[ai.phi4]
model_path = ""
context_tokens = 1024
max_tokens = 64
temperature = 0.85
```

## Missing model behavior

- Wsh does not crash.
- `ai status` reports `Phi-4 model: missing`.
- Commentary provider reports `fallback` or `unavailable`.
- Fallback commentary is used if `ai.commentary.fallback_enabled = true`.
- A log warning is written once at startup.

## Performance rules

- Phi-4 inference runs on a dedicated background worker thread.
- The UI thread, input handling, command execution, and tab completion
  are never blocked.
- The commentary request queue is limited to 5 pending items; old items
  are discarded.
- If commentary is not ready by the time the command finishes, it is
  silently skipped (no retry).
- Commentary is never generated for empty input.

## Safety

- All AI commentary is local. No cloud APIs.
- No Ollama server. No LM Studio server.
- No OpenAI/OpenRouter calls.
- Model is intended to be bundled with the Wsh distribution.

## Manual QA checklist

1. Build Wsh without Phi-4 support.
   Expected: build succeeds, AI fallback works if enabled, no crashes.

2. Build Wsh with Phi-4 support but without model file.
   Expected: terminal starts, `ai status` reports missing model, no crash.

3. Place model at `dist/models/phi-4/model.gguf`.
   Start Wsh.
   Expected: `ai phi4 status` reports loaded or available.

4. Type `tree ?` and press Enter.
   Expected: Wsh prints a short funny `[ai]` line and still executes
   the command normally.

5. Type `history`, `cd ..`, `mkdir test`, `git status`, `rm -rf *`.
   Expected: short commentary appears, command execution is unaffected.

6. Type commands rapidly.
   Expected: terminal input does not freeze.

7. Press Tab completion.
   Expected: no Phi-4 generation is triggered, no crash.

8. Use split terminal panes.
   Expected: commentary appears in the correct pane or is safely
   skipped; never appears in the wrong pane.

9. Run `ai off`.
   Expected: no AI suggestions and no commentary.

10. Run `ai commentary off`.
    Expected: existing `ai suggest/fix/explain` may still work, but
    command commentary is disabled.
