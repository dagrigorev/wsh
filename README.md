# Wsh — ZSH-Compatible Windows Terminal Emulator

A native Windows terminal written in **C11** with zero external dependencies.

| Feature | Detail |
|---------|--------|
| Renderer | Direct2D + DirectWrite (GPU-accelerated) |
| Shell | Built-in ZSH-compatible engine |
| PTY | ConPTY bridge (run any .exe as child) |
| Theme | Catppuccin Mocha (default) |
| Build | CMake + MSVC |
| Tests | CTest unit tests (no external framework) |

---

## Quick Start

```cmd
:: 1. Open "Developer Command Prompt for VS 2022"
mkdir build && cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake

:: 2. Run
Wsh.exe

:: 3. Install (Explorer context menu + PATH)
..\install.bat
```

---

## Requirements

- **Windows 10 1809+** (ConPTY requires 1809; Direct2D on all Win10)
- **Visual Studio Build Tools 2019+** (MSVC cl.exe, nmake/ninja)
- **Windows SDK 10.0.17763+** (bundled with VS Build Tools)
- **Cascadia Code NF** font (optional, for Nerd Font icons) — download from [nerdfonts.com](https://www.nerdfonts.com)

---

## Configuration

On first run, Wsh writes:

| File | Location |
|------|----------|
| `Wsh.toml` | `%APPDATA%\Wsh\Wsh.toml` |
| `.zshrc` | `%USERPROFILE%\.zshrc` |

Edit either file and restart Wsh.

### ZSH-like prompt (default)

```
user@host ~/projects/myapp ❯
```

Prompt is configurable via the `PROMPT` variable in `.zshrc`:

```zsh
PROMPT='%n@%m %~ %# '  # classic ZSH
PROMPT='%~ ❯ '          # minimal
```

### Switching to an external shell

```toml
# Wsh.toml
[general]
shell = "C:\\Program Files\\Git\\bin\\bash.exe"
```

---

## Shell Features

### Syntax (ZSH-compatible)

```zsh
# Variables
FOO=bar; echo $FOO; echo ${FOO:-default}; echo ${#FOO}

# Arithmetic
echo $((2 ** 10))  # 1024

# Control flow
if [ $x -gt 5 ]; then echo big; else echo small; fi
for f in *.txt; do echo $f; done
while read line; do echo $line; done < file.txt

# Functions
greet() { echo "Hello, $1!"; }
greet World

# Pipelines and redirections
ls -la | grep '.c' | sort > results.txt
cat < input.txt | wc -l

# Background jobs
sleep 10 &; jobs; fg
```

### Built-ins

`cd` `echo` `printf` `export` `unset` `alias` `unalias` `source` `.` `exit`
`return` `true` `false` `test` `[` `read` `set` `setopt` `jobs` `fg` `bg`
`kill` `wait` `pwd` `type` `which` `eval` `exec` `local` `typeset` `declare`
`hash` `trap` `open` `clip` `env` `sudo`

### ZSH options

```zsh
setopt AUTO_CD          # cd by typing directory name
setopt CORRECT          # spell correction
setopt GLOB_STAR_SHORT  # ** recursive glob
setopt HIST_IGNORE_DUPS # skip duplicate history
```

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+Shift+C` | Copy |
| `Ctrl+Shift+V` | Paste |
| `Ctrl+Shift+T` | New tab |
| `Ctrl+Shift+=` | Zoom in |
| `Ctrl+Shift+-` | Zoom out |
| `Mouse wheel` / `PgUp/Dn` | Scroll |
| `Ctrl+R` | Reverse history search |
| `Ctrl+A/E` | Start / end of line |
| `Ctrl+K/U` | Kill to end / start |
| `Ctrl+W` | Kill word |
| `Tab` | Complete / show menu |
| `↑ ↓` | History navigation |
| `Ctrl+← →` | Word movement |

---

## Running Tests

```cmd
cd build
ctest --output-on-failure
```

Tests cover: `Arena` · `StrUtil` · `Lexer` · `Parser` · `Expand` · `Builtins` · `History`

See `docs/ARCHITECTURE.md` for a full SOLID/OOP design writeup.

---

## License

MIT — see `LICENSE`.
