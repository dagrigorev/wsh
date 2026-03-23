# Wsh audit report

## Summary

The repository mixes an active modular architecture with legacy duplicate sources. The main executable already targets the modular tree, but include-path wiring and a few runtime behaviors were inconsistent with that intent.

## Key findings

### Build / integration
- `src/main.cpp` and `src/window.cpp` include headers as `core/...`, `shell/...`, `terminal/...`, `platform/...`, but the executable target did not include `src/` itself in include paths.
- `src/repl.c` pointed to nonexistent relative paths (`../shell/...`, `../core/...`) instead of the active modular headers.
- `src/shell/shell_ctx.h` used `strlen` in inline helpers without including `<string.h>`.

### Runtime
- application startup overwrote the user's `~/.zshrc` on every launch
- ConPTY session handles were zero-initialized but later treated as `INVALID_HANDLE_VALUE`-sentinels
- ConPTY shutdown always terminated the child process instead of first checking whether it had already exited
- `default_cwd` was parsed from config but not applied during startup

### Architecture
- docs overstated the amount of completed Zsh/oh-my-zsh compatibility
- root-level duplicate sources are still present and should be removed in a future cleanup phase once verified unused

## Changes made

- corrected executable include paths in `src/CMakeLists.txt`
- corrected include directives in `src/repl.c`
- added missing standard include in `src/shell/shell_ctx.h`
- changed startup logic to seed `~/.zshrc` only when absent
- applied `general.default_cwd` when configured to a concrete path
- initialized ConPTY pipe handles defensively and softened shutdown logic
- corrected misleading docs in `README.md` and `docs/ARCHITECTURE.md`

## Remaining work

- remove or archive legacy root-level duplicate sources
- run the full build and manual GUI/ConPTY verification on Windows with VS 2022
- expand automated regression coverage around prompt rendering, config loading and PTY lifecycle
- define a precise supported subset for Zsh and oh-my-zsh startup files
