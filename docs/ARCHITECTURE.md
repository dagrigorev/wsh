# Wsh architecture

## Actual source of truth

The active implementation is the modular tree:

```
src/core
src/shell
src/terminal
src/platform
```

Root-level duplicates such as `src/shell.c`, `src/builtins.c`, `src/expand.c`, `src/history.c`, `src/jobs.c`, `src/completion.c`, `src/config.c`, `src/pty.c`, `src/screen.c`, `src/vt_parser.c`, `src/renderer.cpp`, `src/font.cpp` are legacy copies and are not linked by the current executable target.

## Main wiring

- `src/main.cpp`
  - loads config
  - creates Win32 window
  - wires renderer, screen buffer and VT parser
  - builds `TerminalIO` implementation of `IShellIO`
  - starts either built-in shell REPL or external ConPTY shell
- `src/repl.c`
  - line editing, prompt display, history navigation, completion dispatch
- `src/window.cpp`
  - window class and initial window creation

## Subsystems

### core
- `arena.*` — arena allocator
- `str_util.*` — UTF-8/UTF-16 helpers and string helpers
- `path_util.*` — path checks and filesystem helpers
- `log.*` — logging helpers

### shell
- `shell_ctx.*` — shell lifecycle and top-level execution API
- `env.*` — variable scopes and process env import/export
- `lexer.*`, `parser.*`, `executor.*` — command language pipeline
- `expand.*` — expansion helpers
- `builtins.*`, `history.*`, `jobs.*`, `completion.*` — user-facing shell features

### terminal
- `screen.*` — cell grid and scrollback
- `vt_parser.*` — ANSI/VT parser
- `renderer.*`, `font.*` — Direct2D/DirectWrite painting

### platform
- `config.*` — TOML-like config parser
- `pty.*` — ConPTY lifecycle and reader thread
- `input.*` — Win32 keyboard translation into terminal/shell input events

## Known architectural gaps

- feature matrix in earlier docs overstated Zsh and oh-my-zsh compatibility
- GUI/runtime behavior is only partially covered by automated tests
- legacy duplicate sources increase maintenance risk until fully removed
- ConPTY shutdown and resize paths need Windows-side behavioral verification

## Recent stabilization changes

- fixed executable include-path wiring so modular headers resolve consistently
- fixed `src/repl.c` includes to target the active modular shell/core headers
- preserved user `~/.zshrc` instead of overwriting it on every launch
- improved ConPTY handle initialization and shutdown behavior
- applied configured `default_cwd` when set to a real path
