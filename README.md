# wsh - A Zsh-inspired Shell for Windows

_pet project made just for fun_
_just for train c++ skills_

## Overview

wsh is a lightweight, customizable command shell for Windows that brings Zsh-like features to all Windows versions. Written in modern C++20, it offers:

- Zsh-style command history and tab completion
- Virtual environments with custom prompts
- Alias support similar to Zsh
- Customizable color themes
- Cross-version Windows compatibility (Windows 7 through 11)

## Features

✨ **Zsh-like Experience**:
- Command history navigation (Up/Down arrows)
- Tab completion for files/directories
- Recursive alias expansion

🎨 **Customizable Themes**:
- Configure colors and prompt symbols
- Environment-specific theming
- ANSI color support

📁 **Virtual Environments**:
- Directory-specific configurations
- Automatic prompt customization
- Environment variable management

⚙️ **Easy Configuration**:
- Single `.wshrc` configuration file
- Supports comments and simple key=value syntax
- Hierarchical configuration (directory > user > system)

## Quick Start

1. Build the project using CMake (requires C++20 compatible compiler)
2. Run `wsh.exe`
3. Create a `.wshrc` file in any directory to customize the environment

## Example `.wshrc`

```sh
# Environment definition
env myproject

# Theme customization
prompt_fg=214     # Orange prompt
dir_fg=81         # Light blue directories
prompt_symbol=λ   # Custom prompt symbol

# Aliases
alias ll='ls -l'
alias gs='git status'

# Environment variables
PYTHONPATH=./src
```

## Commands

- `alias` - Manage command aliases
- `cd` - Change directory (auto-reloads config)
- `exit` - Quit the shell

## Requirements

- Windows 7 or later
- C++20 compatible compiler (for building)
- CMake 3.20+ (for building)

## License

MIT License - Free for personal and commercial use
