# Scrollback fix notes

This pass fixes the first scrollback implementation when the viewport did not move visibly.

## Changes

- Added native Windows vertical scrollbar (`WS_VSCROLL`) as a second, explicit scroll control.
- Added `WM_VSCROLL` handling with inverted terminal semantics:
  - scrollbar bottom = live terminal bottom;
  - scrollbar top = oldest available scrollback line.
- Fixed precision touchpad / high resolution wheel handling by accumulating `WM_MOUSEWHEEL` deltas.
  Previously deltas smaller than `WHEEL_DELTA` were truncated to zero, so scrolling looked broken.
- Added `screen_set_viewport_offset()` so UI code can set an absolute viewport position safely.
- Kept keyboard scrolling:
  - `Ctrl+Shift+Up/Down`
  - `Shift+PageUp/PageDown`
  - `Ctrl+Shift+PageUp/PageDown`
- Synchronizes the native scrollbar after output, resize, keyboard scroll, wheel scroll and PTY notifications.

## QA

Generate enough output to exceed the visible terminal height, then test:

```sh
seq 1 200
```

If `seq` is not available:

```powershell
powershell -NoProfile -Command "1..200 | ForEach-Object { 'line-' + $_ }"
```

Expected behavior:

- mouse wheel scrolls back through output;
- precision touchpad scrolling works;
- native scrollbar appears after scrollback exists;
- dragging the native scrollbar changes the viewport;
- new typed input/output returns the viewport to the live bottom.
