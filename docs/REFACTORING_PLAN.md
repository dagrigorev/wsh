# Wsh refactoring plan: from terminal prototype to full console environment

## Target product

Wsh should evolve into a Windows-first console work environment, not only a terminal window. The long-term shape is:

- `Wsh.exe` — terminal host + built-in shell runtime + ConPTY bridge for external shells.
- `wsh` language/runtime — command execution, environment, scripts, completion, history, jobs.
- companion utilities — `ls`, `cd` as built-in, `md`, `tree`, later `cat`, `cp`, `mv`, `rm`, `grep`, `find`, `touch`, `where`, `which`.
- documentation runtime — every built-in and utility has `--help` and a `man/<topic>.txt` page renderable by `man <topic>` inside Wsh.
- theme system — oh-my-zsh-like named themes, but native to Wsh: prompt/theme/colors/title can be configured and shipped as packages.

## Current code review findings

### Good decisions already present

- The active tree is already modular: `core`, `shell`, `terminal`, `platform`.
- Shell I/O is behind `IShellIO`, which is the right seam for tests and terminal/PTY integration.
- Renderer and VT parser are separated from shell semantics.
- Config is centralized in `src/platform/config.*`.

### Risks and problematic areas

- Legacy duplicate files still exist directly under `src/`; they can confuse future edits and include resolution.
- Config parser is intentionally minimal; it should not silently accept malformed production configs forever.
- Built-ins are in one large file. This is acceptable for a prototype, but it will slow down growth.
- External process command-line quoting is basic and should be hardened before serious scripting usage.
- Manual pages are plain text now. This is stable and simple, but later needs pager/search support.
- Utility aliases in `.zshrc` must only reference capabilities actually implemented by bundled utilities.

## Refactoring phases

### Phase 1 — stabilize runtime shape

- Keep only active modular source files in build inputs.
- Move old root-level `src/*.c|*.h` files to `legacy/` or remove after a build verification pass.
- Make `dist/` the single runnable output folder.
- Require all runtime resources (`config`, `themes`, `man`) to be copied to `dist/`.
- Add a smoke test script that verifies `dist/Wsh.exe`, `dist/wshinit.exe`, `dist/ls.exe`, `dist/md.exe`, `dist/tree.exe`, `dist/themes`, and `dist/man` exist.

### Phase 2 — split built-ins and utilities

- Split `src/shell/builtins.c` into files by domain:
  - `builtins/navigation.c` — `cd`, `pwd`.
  - `builtins/env.c` — `export`, `unset`, `env`, `set`, `typeset`.
  - `builtins/jobs.c` — `jobs`, `fg`, `bg`, `wait`, `kill`.
  - `builtins/docs.c` — `help`, `man`.
  - `builtins/io.c` — `echo`, `printf`, `read`, `clip`.
- Keep a central registration table, but generate command metadata from one structure: name, type, handler, short help, man topic.
- Add a shared `wshlib` for utilities: argument parsing, console output, filesystem helpers, error formatting.

### Phase 3 — documentation system

- Keep `man/*.txt` as the first stable format.
- Add `man --list`, `man --search <term>`, and simple paging.
- Make CI fail when a command exists without a matching man page.
- Add a short `--help` output to every utility and every built-in.

### Phase 4 — theme system

- Current minimal model: `general.theme = "name"` loads `themes/name.toml`.
- Next model:
  - `themes/<name>/theme.toml` for colors/title/prompt.
  - optional `themes/<name>/prompt.wsh` for prompt hooks.
  - `wsh theme list`, `wsh theme use <name>`, `wsh theme preview <name>`.
- Keep built-in themes immutable in `dist/themes`; copy user-customizable themes into `%APPDATA%\Wsh\themes`.

### Phase 5 — shell maturity

- Harden CreateProcess quoting with Windows-compatible escaping rules.
- Add command substitution, pipeline edge cases, and reliable redirection tests.
- Add script mode: `Wsh.exe --exec "command"`, `Wsh.exe script.wsh`.
- Add completion sources for commands, paths, git branches and man topics.

### Phase 6 — product-level console environment

- Add profiles, tabs, sessions and workspaces.
- Add command palette.
- Add update-safe user config migration.
- Add telemetry-free local diagnostics command: `wsh doctor`.
- Add packaging: ZIP, MSI/MSIX later.

## Utility layer follow-up

The current Wsh companion utility layer is now split into two categories:

1. Dedicated utilities with richer behavior:
   - `ls`
   - `tree`
   - `md`
   - `wshinit`

2. Lightweight Windows-native coreutils-compatible commands implemented by `tools/coreutils.cpp` and dispatched by executable name:
   - file/path: `cat`, `pwd`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`, `basename`, `dirname`, `find`
   - text: `head`, `tail`, `wc`, `grep`, `sort`, `uniq`, `cut`, `tee`, `more`, `less`
   - environment/system: `echo`, `env`, `printenv`, `whoami`, `hostname`, `uname`, `date`, `clear`, `which`, `sleep`, `yes`, `true`, `false`

Next refactoring step: extract every utility from `coreutils.cpp` into a small command module with shared option parsing, stable diagnostics, and behavior parity tests against expected POSIX/GNU semantics where possible.
