# Wsh — Windows-first shell and terminal environment

Wsh is a native Windows terminal/shell project built with C11/C++14, Win32, Direct2D/DirectWrite and ConPTY.

The project goal is to become a complete Windows-first console work environment: native terminal host, built-in shell runtime, bundled Unix-like utilities, theme system, local documentation and initialization tooling.

## Current state

Implemented now:

- Win32 window and message loop.
- Direct2D/DirectWrite terminal renderer.
- terminal screen buffer and VT parser.
- built-in shell context, lexer, parser, executor, history, jobs and completion.
- optional external shell hosting through ConPTY.
- config loading from `%APPDATA%\Wsh\Wsh.toml`.
- built-in `help` and `man` commands.
- companion utilities: `ls`, `md`, `tree`, `wshinit`.
- built-in themes: `catppuccin-mocha`, `nord`, `solarized-dark`, `material-ocean`, `wsh-light`.
- CTest-based unit tests for core shell pieces.
- `dist/` runtime folder produced by CMake.

Still not honest to claim as complete:

- full Zsh compatibility.
- full oh-my-zsh plugin compatibility.
- mature package/plugin manager.
- production-grade parser diagnostics and config validation.

## Build

Open a Visual Studio 2022 Developer Command Prompt:

```cmd
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
ctest --output-on-failure
```

Runnable output is placed into:

```text
build\dist
```

Expected contents:

```text
Wsh.exe
ls.exe
md.exe
tree.exe
wshinit.exe
config\
themes\
man\
```

## First run

Initialize user environment:

```cmd
build\dist\wshinit.exe
```

This creates:

```text
%APPDATA%\Wsh\Wsh.toml
%APPDATA%\Wsh\themes\
%APPDATA%\Wsh\man\
```

Launch:

```cmd
build\dist\Wsh.exe
```

Inside Wsh:

```sh
help
man wsh
man ls
ls -l
md demo
tree -L 2 .
```

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
theme = "nord"
title = "Wsh - ${cwd}"
```

`general.shell = "wsh"` uses the built-in shell. Any other command path is hosted through ConPTY.

Title placeholders:

- `${cwd}` — current working directory.
- `${theme}` — configured theme name.
- `${version}` — Wsh version.

## Themes

Theme files are TOML files with a `[colors]` section.

Search order:

1. `dist\themes\<theme>.toml`
2. `%APPDATA%\Wsh\themes\<theme>.toml`
3. direct path if `general.theme` contains a slash, backslash or drive separator

Built-in themes:

- `catppuccin-mocha`
- `nord`
- `solarized-dark`
- `material-ocean`
- `wsh-light`

## Repository map

- `src/core` — strings, paths, arena, logging.
- `src/shell` — lexer/parser/executor/builtins/history/jobs/completion.
- `src/terminal` — screen, VT parser, renderer, font.
- `src/platform` — config, ConPTY, input translation.
- `src/main.cpp`, `src/window.cpp`, `src/repl.c` — app wiring and UI loop.
- `tools` — companion utilities.
- `themes` — built-in themes.
- `man` — bundled manual pages.
- `tests` — unit tests.
- `docs/REFACTORING_PLAN.md` — planned evolution into a complete console environment.

## Notes

The repository still contains older legacy duplicate files directly under `src/`. The active implementation is the modular tree under `src/core`, `src/shell`, `src/terminal`, and `src/platform`.

## Error logging

WSH writes runtime diagnostics and crash details to:

```text
%LOCALAPPDATA%\Wsh\logs\wsh.log
```

If `LOCALAPPDATA` is unavailable, WSH falls back to `logs\wsh.log` near the executable. The log file is capped at **10 MiB**. When the next write would exceed the cap, the file is truncated and logging continues from the beginning.

Logged events include startup/shutdown, Win32 API failures, PTY errors, renderer/font initialization failures, unhandled C++ exceptions, CRT invalid-parameter failures, process signals, and unhandled structured exceptions.


## Companion utilities

- `ls`, `tree`
- `cat`, `pwd`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`
- `head`, `tail`, `wc`, `grep`, `sort`, `uniq`
- `basename`, `dirname`, `which`
- `whoami`, `hostname`, `uname`, `date`, `clear`, `sleep`, `yes`, `true`, `false`
- `echo`, `env`, `printenv`, `more`, `less`, `find`, `tee`, `cut`
- `md`, `wshinit`
