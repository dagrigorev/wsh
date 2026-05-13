# WSH pass4: internal terminal panes

`zsh` does not implement window splitting itself. Pane splitting is normally a
terminal/multiplexer feature (`tmux`, `screen`, Windows Terminal panes). This pass
ports the user-facing concept into WSH's own terminal layer.

## Implemented

- Up to 4 internal terminal panes in one WSH window.
- Each pane owns an independent:
  - `ScreenBuffer`
  - `VtParser`
  - `ShellContext`
  - `Repl`
  - optional `PtySession`
- Active pane focus with a highlighted border.
- Independent scrollback per pane.
- Independent input routing per pane.
- Mouse click focuses a pane.
- Window resize recalculates each pane's PTY/screen size.

## Hotkeys

- `Ctrl+Shift+D` — create/split a new pane.
- `Ctrl+Shift+W` — close a pane.
- `Alt+Left` / `Alt+Up` — focus previous pane.
- `Alt+Right` / `Alt+Down` — focus next pane.
- Existing scroll shortcuts continue to work for the active pane.

## Current layout strategy

- 1 pane: full window.
- 2 panes: vertical split.
- 3 panes: left full-height pane + two right stacked panes.
- 4 panes: 2x2 grid.

This keeps the first implementation deterministic and simple. A later pass can
replace this with a real split tree supporting arbitrary nested ratios.

## Important implementation note

ConPTY callbacks store a `TerminalPane*` as userdata. Because of that, panes are
not memmoved while a PTY reader may still be alive. The first close implementation
closes the last physical pane safely. A future pane manager should use stable heap
allocation per pane and a split tree to allow arbitrary close/reparent operations.

## QA

Inside WSH:

```text
Ctrl+Shift+D
Alt+Right
эcho or type text in the new pane
Alt+Left
run another command in the first pane
Ctrl+Shift+D again
Alt+Right / Alt+Left to cycle focus
Ctrl+Shift+W to close a pane
```

For scrollback:

```powershell
powershell -NoProfile -Command "1..200 | ForEach-Object { 'pane line ' + $_ }"
```

Then use mouse wheel or `Shift+PageUp` in the active pane.
