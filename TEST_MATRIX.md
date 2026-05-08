# Test matrix

This file separates what is covered automatically from what still needs real Windows GUI verification.

## Automated coverage added

- `test_config` — default config overrides, TOML parsing, color parsing
- `test_completion` — alias completion, environment-variable completion, completion application
- `test_executor` — builtin execution, pure assignments, single-level alias expansion
- `test_screen` — character placement, wrapping, wide glyph bookkeeping, resize preservation, scrollback
- `test_vt_parser` — text emission, SGR attributes, OSC title, alternate-screen switching
- `test_layout` — grid recalculation under effective scale changes, cell hit-testing, startup/welcome message visibility thresholds

## What these layout tests verify for display logic

The new layout tests do not draw pixels, but they verify the math that determines:

- how many rows/columns remain visible at different effective text scales
- whether the startup/welcome block plus prompt still fits after compression
- whether pointer-to-cell mapping remains clamped after resize

That means the logic behind text fitting during DPI changes and window compression is now covered by deterministic tests.

## Manual Windows checks still required

These must be run on Windows because they depend on the real Win32 + Direct2D + DirectWrite + window-manager path:

1. Launch Wsh at 100%, 125%, 150%, and 200% display scale.
2. Verify startup message and prompt are fully visible on first paint.
3. Shrink the window horizontally and vertically until only a few rows remain.
4. Confirm text remains aligned to the cell grid and does not overlap or double-draw.
5. Minimize the window and restore it.
6. Maximize the window and restore it.
7. Confirm the welcome/startup lines are not duplicated, truncated mid-repaint, or replaced with stale content.
8. Repeat with an external PTY-backed shell.

## Expected results for the welcome/startup message

- Compression may reduce visible rows, but rendering should stay grid-aligned.
- Restore/maximize should not duplicate the welcome block.
- Cursor/prompt should remain on the active input line after restore.
- Increasing DPI/text size should reduce rows/columns predictably, not corrupt existing buffer content.
