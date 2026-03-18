# WSH Terminal

WSH Terminal is a Windows-first terminal emulator MVP built with Win32, ConPTY, Direct2D and DirectWrite.

## Current implementation

Implemented now:
- native Win32 desktop window
- ConPTY-backed terminal sessions
- DirectWrite text rendering
- VT parsing for printable text, CR/LF/BS/TAB and a practical subset of CSI commands
- multiple tabs and launch profiles
- scrollback
- mouse selection
- clipboard copy/paste
- mouse wheel scrolling
- window resize propagation to ConPTY
- UTF-8/UTF-16 conversion helpers for Cyrillic-safe input/output
- simple TOML-like profile/theme/settings loader for the bundled config file

Not finished yet:
- split panes
- alternate screen buffer fidelity for full-screen TUIs
- advanced key mapping
- command palette
- built-in custom shell profile
- plugin model
- rich prompt engine

## Build in Visual Studio 2026

Requirements:
- Visual Studio 2026 with Desktop development with C++
- Windows 10 1903+ or Windows 11
- Windows SDK with ConPTY support
- CMake support installed in Visual Studio

Open the repository folder in Visual Studio and choose the `vs2026-x64-debug` or `vs2026-x64-release` CMake preset.

Or from the Developer PowerShell:

```powershell
cmake --preset vs2026-x64-debug
cmake --build --preset build-debug
```

The executable expects `assets/wsh_profiles.toml` next to the binary or in the working directory.

## Controls

- `Ctrl+T` — open a new tab with the first profile
- `Ctrl+W` — close active tab
- `Ctrl+Tab` — next tab
- `Ctrl+Shift+Tab` — previous tab
- `Ctrl+Shift+C` — copy selection
- `Ctrl+Shift+V` — paste clipboard text
- `Mouse wheel` — scroll scrollback
- `Left mouse drag` — selection
- `F1/F2/F3` — open PowerShell / CMD / WSL profile

## Notes

This codebase is intentionally organized around subsystem boundaries so it can grow into a larger terminal + shell workspace later.
