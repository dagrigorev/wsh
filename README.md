# Wsh — Windows-first shell and terminal prototype

Wsh is a native Windows terminal/shell prototype built with C11/C++14, Win32, Direct2D/DirectWrite and ConPTY.

## Current project state

This repository currently contains two code generations:

- the active modular implementation under `src/core`, `src/shell`, `src/terminal`, `src/platform`
- older legacy duplicates under `src/*.c` and `src/*.h`

The executable target is wired to the modular implementation. The legacy root-level duplicates are retained only as reference and should not be treated as the source of truth.

## What is implemented now

- Win32 window creation and message loop
- Direct2D/DirectWrite renderer
- terminal screen buffer and VT parser
- built-in shell context, lexer, parser, executor, history, jobs, completion
- optional external shell hosting through ConPTY
- config loading from `%APPDATA%\Wsh\Wsh.toml`
- initial user config seeding from bundled `config/.zshrc` when the file does not already exist
- CTest-based unit tests for core shell pieces

## What is not yet honest to claim as fully complete

The codebase is **not** at full Zsh or oh-my-zsh compatibility. The current implementation provides a pragmatic subset and should be described as:

- **partial Zsh-style syntax and prompt compatibility**
- **best-effort `.zshrc` sourcing**
- **limited compatibility with common alias/prompt patterns**
- **no full oh-my-zsh plugin/theme compatibility guarantee**

## Build

Open a Visual Studio 2022 Developer Command Prompt:

```cmd
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
ctest --output-on-failure
```

## Runtime notes

- `~/.zshrc` is now seeded only when it does not exist; user changes are preserved.
- `general.shell = "wsh"` uses the built-in shell.
- any other configured shell path runs through ConPTY.

## Repository map

- `src/core` — strings, paths, arena, logging
- `src/shell` — lexer/parser/executor/builtins/history/jobs/completion
- `src/terminal` — screen, VT parser, renderer, font
- `src/platform` — config, ConPTY, input translation
- `src/main.cpp`, `src/window.cpp`, `src/repl.c` — app wiring and UI loop
- `tests` — unit tests

## Testing

The included tests now cover:

- core utilities and arena helpers
- lexer, parser, prompt expansion and word expansion
- builtins, executor basics, history and completion
- config parsing
- terminal screen buffer and VT parser behavior
- renderer/layout calculations for DPI-sensitive grid sizing and startup-message visibility

True Win32 GUI, Direct2D drawing, minimize/restore, and ConPTY behavior still require manual verification on a real Windows machine. See `TEST_MATRIX.md`.
