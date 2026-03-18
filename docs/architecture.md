# Architecture

## Layers

### app
Owns the Win32 window, message loop integration, Direct2D/DirectWrite setup, input routing, tab strip and status bar rendering.

### workspace
Owns the list of terminal tabs and the active tab index.

### terminal
Owns screen state, selection data, scrollback, VT parsing and tab-level orchestration.

### conpty
Owns pseudoconsole creation, process spawning, pipes and reader thread.

### config
Loads theme and profiles from the bundled settings file.

### core
Logging and UTF conversions.

## Current rendering model

Rendering is grid based.
Each terminal cell is drawn individually using DirectWrite text output.
This is simple and reliable for an MVP, but later it should be replaced with row batching and glyph run rendering for performance.

## Current VT support

Implemented:
- printable UTF-8 text
- CR/LF/BS/TAB
- CSI H/f
- CSI J with `2J`
- CSI K
- CSI m with basic 16-color SGR subset

## Next steps

1. introduce a terminal controller event queue to decouple ConPTY reader thread from screen mutation
2. add alternate buffer, origin modes and better wrapping rules
3. replace timer-based refresh with invalidate-on-output via posted window messages
4. add profile switcher UI and clickable tabs
5. add split panes and pane focus model
6. add custom shell profile
