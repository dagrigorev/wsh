# Wisp

Wisp is a fast, native terminal emulator for Windows, written in C/C++ on Win32, Direct2D/DirectWrite and ConPTY.

Wisp is a terminal emulator and nothing else. It does not implement a shell — it hosts one. PowerShell, `cmd.exe`, WSL, Git Bash or anything else that speaks to a console runs inside it, and Wisp draws what that program writes.

**Build**: 5 targets, zero errors — **Tests**: 6/6 passing (100%)

## Relationship to Ghostty

Wisp is an independent project, heavily inspired by [Ghostty](https://github.com/ghostty-org/ghostty) — its design philosophy, its scope and its defaults. It is **not** affiliated with, endorsed by, or a port of the Ghostty project, and it shares no code with it. Ghostty is written in Zig and targets macOS and Linux; Wisp is written in C/C++ and targets Windows.

What Wisp takes from Ghostty is the shape of the thing: a terminal emulator that renders on the GPU, uses platform-native UI, ships almost no chrome, and leaves shell behaviour to the shell.

Wisp does not attempt to match Ghostty feature for feature. Ghostty is a large, mature project; Wisp is a young one. See [Not Implemented](#not-implemented) for the gap.

## Current State

Implemented:

- Native Win32 window with per-monitor DPI awareness.
- Direct2D/DirectWrite renderer: 256-color palette, truecolor, bold/italic/underline/blink/reverse/strikethrough, CJK double-width cells.
- Terminal screen buffer and VT/ANSI parser: cursor control, scroll regions, alternate screen, erase/insert/delete, DEC private modes, OSC window title, UTF-8 decoding, DEC Special Graphics.
- Scrollback (configurable, default 10,000 lines) with mouse wheel and keyboard scrolling.
- Shell hosting over ConPTY, with auto-detection: PowerShell 7, then Windows PowerShell, then `%COMSPEC%`.
- Tabs (up to 16) and splits (up to 4 panes per tab, horizontal or vertical).
- Mouse selection, copy on selection, bracketed paste.
- Font zoom, cursor styles, cursor blink.
- TOML config at `%APPDATA%\Wisp\Wisp.toml` with sections for general, font, cursor, colors, keybinds, tabs and scrollbar.
- Six built-in themes.
- Runtime logging to `%LOCALAPPDATA%\Wisp\logs\wisp.log` (10 MiB cap, auto-truncate).
- CTest unit tests for the arena allocator, string utilities, config parser, screen buffer, VT parser and grid layout.
- GitHub Actions CI on every push and pull request, plus tagged release builds.

## Not Implemented

Wisp does not yet have, and should not be described as having:

- Kitty graphics protocol, Sixel, or any inline image support.
- The Kitty keyboard protocol.
- Synchronized output (DEC 2026).
- Ligature shaping (the config flag exists; the renderer does not act on it).
- Mouse reporting to the hosted program (SGR/X10 mouse modes).
- Arbitrary split trees — splits are a flat list of up to four panes, all divided along one axis.
- Split resizing, split zoom, or dragging tabs.
- A configuration UI, live config reload, or config validation diagnostics.
- Session save and restore.
- Any packaging beyond a ZIP: no winget, Scoop or Chocolatey manifest.

## Build And Run

Requires Visual Studio 2022 (or Build Tools) with the C++ workload, and CMake 3.20+.

```powershell
.\build-and-run-wisp.ps1
```

Options:

| Flag | Effect |
|---|---|
| `-Configuration Debug` | Debug build (default: `Release`) |
| `-BuildDir <dir>` | Build directory (default: `build-run`) |
| `-Clean` | Delete the build directory first |
| `-NoRun` | Build without launching |
| `-RunTests` | Run CTest after building |

The script locates the Visual Studio environment itself, so it works from an ordinary PowerShell prompt.

To build manually from a Developer Command Prompt:

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable lands in `build\dist\Wisp.exe`.

## Keybindings

| Keys | Action |
|---|---|
| `Ctrl+Shift+C` | Copy selection |
| `Ctrl+Shift+V` | Paste |
| `Ctrl+Shift+T` | New tab |
| `Ctrl+Shift+W` | Close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Ctrl+Shift+E` | Split vertically |
| `Ctrl+Shift+O` | Split horizontally |
| `Ctrl+Shift+]` / `Ctrl+Shift+[` | Cycle panes |
| `Ctrl+Shift+=` / `Ctrl+Shift+-` | Zoom in / out |
| `Shift+PageUp` / `Shift+PageDown` | Scroll by page |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Scroll by line |

Selecting with the mouse copies automatically. Right-click pastes.

## Configuration

Wisp reads `%APPDATA%\Wisp\Wisp.toml` and writes a default one on first run.

```toml
[general]
shell = ""                        # empty = auto-detect (pwsh, powershell, COMSPEC)
scrollback = 10000
confirm_exit = true
bell = "visual"
default_cwd = "~"
theme = "material-cyber-dark"
title = "Wisp - ${cwd}"

[font]
family = "Cascadia Code"
size = 13.0

[cursor]
style = "block"                   # block | bar | underline
blink = true
blink_rate_ms = 530

[tabs]
enabled = true
position = "top"
max_tabs = 20
```

Set `shell` to any executable to host it instead: `"wsl.exe"`, `"cmd.exe"`, a Git Bash path, and so on. Wisp refuses to host itself.

Themes are TOML files in the `themes` directory next to the executable, or in `%APPDATA%\Wisp\themes`. Set `general.theme` to a file name without the extension.

## Layout

- `src/core` — arena allocator, string/path/Unicode utilities, structured logger.
- `src/terminal` — screen buffer, VT/ANSI parser, Direct2D renderer, DirectWrite font state, grid layout.
- `src/platform` — TOML config parser, ConPTY lifecycle, Win32 keyboard translation.
- `src/main.cpp`, `src/window.cpp` — window creation, message loop, tabs, splits, clipboard, chrome.
- `themes` — built-in TOML themes.
- `tests` — unit tests.
- `tools/make_icon.py` — regenerates `res/wisp.ico`.
- `.github/workflows/ci.yml` — build and test on push and pull request.
- `.github/workflows/release.yml` — tagged release builds, ZIP packaging with SHA256, GitHub Release publishing.

## Logging

Wisp writes diagnostics and crash details to:

```
%LOCALAPPDATA%\Wisp\logs\wisp.log
```

The file is capped at 10 MiB and truncated automatically.

## License

MIT.
