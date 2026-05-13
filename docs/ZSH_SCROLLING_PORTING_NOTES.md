# Zsh-like scrolling pass for WSH

This pass ports the practical scrolling behaviour expected from zsh running in a terminal, rather than copying zsh internals verbatim.

## What was ported conceptually

Zsh itself relies on the terminal for the normal output scrollback. Its own scrolling code is mostly about ZLE and completion-list paging (`listscroll`, `LISTPROMPT`, `select-scroll`). For WSH the equivalent responsibility lives in the terminal layer:

- a bounded primary-screen scrollback buffer;
- a viewport offset independent from the live cursor position;
- keyboard/mouse viewport navigation;
- alternate-screen isolation for full-screen apps such as `less`, editors, and TUIs;
- `ED 3` support through the existing erase-display path to clear scrollback;
- output returning the viewport to the live bottom, matching the expected interactive shell behaviour.

## Files changed

- `src/terminal/screen.h`
- `src/terminal/screen.c`
- `src/terminal/renderer.cpp`
- `src/platform/input.h`
- `src/platform/input.c`
- `src/main.cpp`
- `tests/test_screen.c`

## Behaviour

- Mouse wheel scrolls the scrollback viewport.
- `Ctrl+Shift+Up/Down` scrolls by a few lines.
- `Shift+PageUp/PageDown` and `Ctrl+Shift+PageUp/PageDown` scroll by page.
- New output resets viewport to live bottom.
- Alternate screen disables normal scrollback viewing.
- Renderer now composes visible rows from both scrollback and the live primary screen.
- Optional scrollbar from `[scrollbar]` config is rendered as a simple overlay when scrollback exists.

## Known limitations

- Scrollback is still line-grid based; it does not reflow old lines when the window width changes.
- Selection coordinates are still viewport-local and should be upgraded later to logical scrollback coordinates.
- The visual scrollbar is passive; dragging it is not implemented yet.
