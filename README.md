# Wsh

Wsh is a Windows-first shell and terminal environment written in C/C++ with CMake, Win32, Direct2D/DirectWrite, and ConPTY.

The project combines a real terminal host, a modular ZSH-compatible shell runtime, split panes, VT/ANSI rendering, scrollback, bundled Unix-like utilities, local manual pages, themes, an AI assistant subsystem (n-gram prediction, tiny LLM, Phi-4 commentary), session save/restore, and a Material Cyber UI.

**Build**: 52 targets, zero errors — **Tests**: 61/61 passing (100%)

## Screenshots

The screenshots below are generated from real Wsh bundle utilities and repository state. They do not use demo sessions or hardcoded output.

![main window](docs/screenshots/main.png)

![tree with Git markers](docs/screenshots/tree-git-marks.png)

![htop process snapshot](docs/screenshots/htop-processes.png)

![ping ICMP output](docs/screenshots/ping-icmp.png)

## Current State

Implemented now:

- Native Win32 window and message loop.
- NeoTerm-inspired Material Cyber terminal layout with tabs, toolbar, sidebar, terminal header, viewport, and status bar.
- Direct2D/DirectWrite terminal renderer with 256-color palette, truecolor, bold/italic/underline/blink/reverse/strikethrough, CJK double-width.
- Terminal screen buffer, VT/ANSI escape sequence parser, cursor rendering, selection, resize, alternate screen, and scrollback (configurable, default 10K).
- Split panes (1–4) and active pane routing.
- Modular ZSH-compatible built-in shell — lexer, parser, executor, 48 built-in commands, history (ring buffer, 50K max, reverse search, file persistence), jobs (64 max, fg/bg/kill), completion, variable expansion, globbing, brace expansion, arithmetic, command substitution, heredocs, pipelines, if/while/for/function/case, `precmd`/`preexec` hooks.
- Optional external shell hosting through ConPTY (cmd.exe, PowerShell, WSL, etc.).
- Config loading from `%APPDATA%\Wsh\Wsh.toml` with sections for general, font, cursor, colors, keybinds, tabs, scrollbar, session, and AI.
- Built-in `help`, `man`, `history`, and `ai` commands with 43 bundled manual pages.
- AI assistant subsystem:
  - **N-gram prediction**: 4-gram statistical model for command completion trained from a command corpus.
  - **Tiny LLM**: Micro reasoning model (~2 MB) for proactive suggestions during typing, displayed as subtitles below the prompt.
  - **Rule-based fallback**: Heuristic suggestions when models are unavailable.
  - **Phi-4 commentary**: Post-execution command analysis via Microsoft Phi-4 GGUF model, with both direct in-process (llama.cpp) and subprocess runtimes.
  - **Backtick queries**: `` `natural language question` `` queries the AI inline.
  - **Context awareness**: Detects CMake, Node.js, .NET, and Git projects for context-sensitive suggestions.
- In-shell task scheduler — one-shot and repeating delayed commands (`at`/`atq`/`atrm`).
- Session persistence — save and restore tabs, panes, scrollback, and cursor state across restarts.
- Runtime logging under `%LOCALAPPDATA%\Wsh\logs\wsh.log` (10 MiB cap, auto-truncate).
- Built-in themes (6) and companion utilities (8 binaries providing 35+ commands).
- CTest-based tests for shell, terminal, utility, and UI state behavior.
- GitHub Actions CI/CD — automatic release builds, packaging, and publishing on tagged commits.
- PowerShell and batch installers with App Paths registration, Explorer context menus, PATH setup, and desktop shortcuts.

Still not honest to claim as complete:

- Full Zsh compatibility.
- Full oh-my-zsh plugin compatibility.
- Mature package/plugin manager.
- Complete shell integration for every external shell.
- Production-grade parser diagnostics and config validation.

## Build And Run

The easiest local workflow is the project script:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build-and-run-wsh.ps1 -NoRun -RunTests
```

This configures, builds, copies runtime assets, and runs the test suite. Runnable output is placed into:

```text
build-run\dist
```

Launch Wsh:

```powershell
.\build-run\dist\Wsh.exe
```

Manual CMake flow:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Build options:

| Option | Description |
|---|---|
| `-DWSH_ENABLE_PHI4=ON` | Fetch llama.cpp and enable direct in-process Phi-4 inference |
| `-DWSH_DOWNLOAD_PHI4_MODEL=ON` | Download Phi-4 GGUF model during build (~14 GB) |
| `-DWSH_DOWNLOAD_LLAMA_CLI=ON` | Download llama-cli.exe for subprocess Phi-4 inference |
| `-DWSH_BUILD_TESTS=OFF` | Skip building tests |

## First Run

Initialize the user environment:

```powershell
.\build-run\dist\wshinit.exe
```

This creates:

```text
%APPDATA%\Wsh\Wsh.toml
%APPDATA%\Wsh\themes\
%APPDATA%\Wsh\man\
```

Inside Wsh:

```sh
help
man wsh
man tree
man ping
man htop
tree -L 2 .
ping -n 4 127.0.0.1
htop -n 20 -s mem
```

## Installation

**PowerShell installer** (builds from source, full setup):
```powershell
.\install-wsh.ps1
```

Options: `-NoBuild`, `-NoContextMenu`, `-NoPath`, `-NoShortcut`, `-Clean`, `-Configuration <Debug|Release>`, `-InstallDir <path>`, `-BuildDir <path>`.

**Batch installer** (pre-built binary, lightweight):
```cmd
.\install.bat
```

Both installers register App Paths, add Explorer context menus ("Open in WSH" for folders, drives, and background), optionally add to PATH, and optionally create a desktop shortcut.

## UI

The main window is organized as:

- Title area with native Windows controls.
- Real terminal tab strip (up to 8 tabs).
- Toolbar with active path and session badge.
- Optional sidebar with real sessions, resources, and actions.
- Terminal header with active shell, PID, path, and state.
- Terminal viewport backed by the real renderer and screen buffer.
- Blue status bar with runtime state such as Git branch, folder, shell, encoding, size, pane index, and time.
- AI reasoning overlay (3 extra rows below the viewport) showing proactive suggestions and commentary.

The UI intentionally avoids fake data. Values shown in tabs, path bars, status badges, resource meters, and utility output come from active Wsh/session/system state. Unsupported data is hidden or shown as unavailable rather than invented.

## Companion Utilities

Bundled utilities are implemented as standalone EXEs — a multi-call `coreutils` binary plus separate dedicated tools:

- **Files and directories**: `ls`, `tree`, `cat`, `pwd`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`
- **Text processing**: `head`, `tail`, `wc`, `grep`, `sort`, `uniq`, `cut`, `tee`
- **Path lookup**: `basename`, `dirname`, `which`
- **System and shell helpers**: `whoami`, `hostname`, `uname`, `date`, `clear`, `sleep`, `yes`, `true`, `false`, `echo`, `env`, `printenv`
- **Viewing and search**: `more`, `less`, `find`
- **Windows-friendly helpers**: `md`, `wshinit`
- **Network and monitoring**: `ping`, `htop`

`tree` displays real filesystem data, colorizes folders/files, and marks Git state when available:

- `[C]` tracked and clean
- `[M]` modified
- `[?]` untracked
- `[A]`, `[D]`, `[R]`, `[U]` for added, deleted, renamed, and conflicted states

`ping` uses real ICMP requests on Windows and includes structured, colored output with latency and packet statistics.

`htop` shows a real process snapshot with PID, CPU, memory, working set, and process name. It supports sorting, row limits, refresh interval, watch mode, and ANSI color control.

## Configuration

Main config:

```text
%APPDATA%\Wsh\Wsh.toml
```

Example:

```toml
[general]
shell = "wsh"
scrollback = 10000
confirm_exit = true
bell = "visual"
default_cwd = "~"
theme = "material-cyber-dark"
title = "Wsh - ${cwd}"

[font]
family = "Cascadia Code"
size = 11
ligatures = true

[cursor]
style = "block"
blink = false

[keybinds]
copy = "Ctrl+Shift+C"
paste = "Ctrl+Shift+V"
new_tab = "Ctrl+Shift+T"
close_tab = "Ctrl+Shift+W"

[scrollbar]
visible = true

[session]
auto_save = true

[ai]
enabled = true
phi4_model_path = "models/phi-4/model.gguf"
phi4_ctx_tokens = 4096
phi4_max_tokens = 256
phi4_temperature = 0.7
```

`general.shell = "wsh"` uses the built-in shell. Any other command path is hosted through ConPTY.

Title placeholders:

- `${cwd}` — current working directory.
- `${theme}` — configured theme name.
- `${version}` — Wsh version.
- `${ai}` — AI-enabled status indicator.

Default shell config (`~/.zshrc`):

Bundled as `config/.zshrc` with history settings, aliases (ll, la, .., ...), convenience functions (mkcd, take, extract), and a precmd hook for Git branch display. Supports sourcing a `~/.zshrc.local` override.

## Themes

Theme files are TOML files with a `[colors]` section.

Search order:

1. `dist\themes\<theme>.toml`
2. `%APPDATA%\Wsh\themes\<theme>.toml`
3. Direct path if `general.theme` contains a slash, backslash, or drive separator.

Built-in themes:

- `material-cyber-dark`
- `catppuccin-mocha`
- `nord`
- `solarized-dark`
- `material-ocean`
- `wsh-light`

## Repository Map

- `src/core` — arena allocator, string/path/unicode utilities, structured logger, session serialization.
- `src/shell` — modular ZSH-compatible shell: lexer, parser, executor, 48 builtins, env, expand, history, jobs, completion, scheduler, shell context.
- `src/terminal` — screen buffer, VT/ANSI parser, Direct2D renderer, DirectWrite font, grid layout.
- `src/platform` — TOML config parser, ConPTY lifecycle, Win32 keyboard input translation.
- `src/ai` — AI provider framework: n-gram model, tiny LLM, rule-based provider, Phi-4 commentary (direct + subprocess runtimes), reasoning model, context detection.
- `src/main.cpp`, `src/window.cpp`, `src/repl.c`, `src/man_viewer.c` — app wiring, window procedure, REPL, and man page viewer.
- `tools` — companion utilities: `coreutils` (multi-call binary, 28 commands), `tree`, `ping`, `htop`, `md`, `wshinit`.
- `themes` — 6 built-in TOML themes.
- `man` — 43 bundled manual pages.
- `config` — default `.zshrc` shell configuration.
- `models` — Phi-4 GGUF model directory (placeholder, downloaded on demand).
- `tests` — 28 unit test files (61 test cases).
- `docs/screenshots` — README screenshots generated from real bundle output.
- `docs/ARCHITECTURE.md` — subsystem mapping and known gaps.
- `docs/REFACTORING_PLAN.md` — planned evolution into a complete console environment.
- `.github/workflows/release.yml` — CI/CD: builds on tag push, packages ZIP with SHA256, publishes GitHub Release.

## AI Subsystem

Wsh includes a layered AI assistant with four providers and a commentary engine:

| Component | Description |
|---|---|
| **N-gram predictor** | Statistical 4-gram model (~74 KB) trained from `tools/command_corpus.txt` for next-command prediction |
| **Tiny LLM** | Micro reasoning model (~2 MB) providing proactive intent detection and suggestions during typing |
| **Rule-based fallback** | Heuristic suggestions when no statistical model is available |
| **Phi-4 commentary** | Post-execution analysis via Microsoft Phi-4 (~14 GB GGUF), with direct in-process (llama.cpp) or subprocess fallback |

Built-in AI commands: `ai status`, `ai suggest`, `ai explain`, `ai fix`. The AI can also answer backtick-quoted questions inline (`` `how do I find large files?` ``).

## Error Logging

Wsh writes runtime diagnostics and crash details to:

```text
%LOCALAPPDATA%\Wsh\logs\wsh.log
```

If `LOCALAPPDATA` is unavailable, Wsh falls back to `logs\wsh.log` near the executable. The log file is capped at 10 MiB. When the next write would exceed the cap, the file is truncated and logging continues from the beginning.

Logged events include startup/shutdown, Win32 API failures, PTY errors, renderer/font initialization failures, unhandled C++ exceptions, CRT invalid-parameter failures, process signals, and unhandled structured exceptions.
