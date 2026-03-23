# ════════════════════════════════════════════════════════════════════════════
#  ~/.zshrc  —  Wsh built-in shell configuration
#  Sourced automatically when Wsh starts in built-in shell mode.
#  Syntax is ZSH-compatible; all options work in both Wsh and real ZSH.
# ════════════════════════════════════════════════════════════════════════════

# ── Shell options ─────────────────────────────────────────────────────────────
setopt AUTO_CD              # type a directory name to cd into it
setopt CORRECT              # offer spelling corrections
setopt GLOB_STAR_SHORT      # ** matches recursively
setopt HIST_IGNORE_DUPS     # don't record duplicate history entries
setopt HIST_IGNORE_SPACE    # commands starting with a space are not recorded
setopt SHARE_HISTORY        # write history after every command
setopt NO_BEEP              # silence the bell (we use visual bell in Wsh.toml)
setopt EXTENDED_GLOB        # extended glob patterns: ^, ~, (#i)
setopt PROMPT_SUBST         # enable $(...) in PROMPT

# ── History ───────────────────────────────────────────────────────────────────
HISTFILE=~/.wsh_history
HISTSIZE=50000
SAVEHIST=50000

# ── Prompt (ZSH-style with colour via ANSI escapes) ───────────────────────────
#
#  Renders as:   user@host ~/projects/myapp  ❯
#                ^green    ^blue              ^colour changes on error
#
#  Colour codes:  \e[0m  reset
#                 \e[1m  bold
#                 \e[32m green   \e[34m blue   \e[31m red
#                 \e[35m magenta \e[36m cyan    \e[33m yellow
#
#  %n  = username      %m  = hostname (short)
#  %~  = current dir   %#  = # if root, $ otherwise
#  %?  = last exit code
#  %(?.✓.✗)  = conditional: ✓ if $?==0 else ✗

# Left prompt: user@host cwd ❯
PROMPT=$'\e[1;32m%n\e[0m\e[2m@\e[0m\e[1;34m%m\e[0m \e[1;36m%~\e[0m %(?..\e[31m[%?] \e[0m)\e[1;35m❯\e[0m '

# Right prompt: git branch (if in a git repo) — shown on the right side
# Wsh evaluates RPROMPT automatically if set.
RPROMPT=''

# ── Colours for ls / dir output ───────────────────────────────────────────────
# On Windows, 'ls' may be Git Bash's ls or busybox ls.  Set colours anyway.
export LS_COLORS='di=1;34:ln=36:so=35:pi=33:ex=1;32:bd=34;46:cd=34;43:su=30;41:sg=30;46:tw=30;42:ow=30;43:'
alias ls='ls --color=auto'
alias ll='ls -lh --color=auto'
alias la='ls -lha --color=auto'
alias lt='ls -lht --color=auto'

# ── Safer file operations ─────────────────────────────────────────────────────
alias rm='rm -i'
alias cp='cp -i'
alias mv='mv -i'

# ── Convenience aliases ───────────────────────────────────────────────────────
alias ..='cd ..'
alias ...='cd ../..'
alias ....='cd ../../..'
alias grep='grep --color=auto'
alias egrep='egrep --color=auto'
alias fgrep='fgrep --color=auto'
alias cls='echo -e "\033[2J\033[H"'          # clear screen
alias reload='source ~/.zshrc'
alias which='type -a'

# Windows-specific aliases
alias notepad='notepad.exe'
alias explorer='explorer.exe'
alias clip='clip'                             # Wsh built-in: pipe to clipboard
alias pbcopy='clip'                           # macOS-style alias

# ── Keyboard bindings (ZLE — Wsh readline) ────────────────────────────────────
# These are no-ops in Wsh's built-in shell but kept for ZSH compatibility.
# bindkey '^[[A' up-line-or-history         # Up arrow
# bindkey '^[[B' down-line-or-history       # Down arrow
# bindkey '^R'   history-incremental-search-backward

# ── Functions ─────────────────────────────────────────────────────────────────

# mkcd: make a directory and cd into it
mkcd() {
    mkdir -p "$1" && cd "$1"
}

# up N: go up N directory levels
up() {
    local n=${1:-1}
    local path=""
    for i in $(seq 1 $n); do
        path="../$path"
    done
    cd "$path"
}

# extract: universal archive extractor
extract() {
    if [ -f "$1" ]; then
        case "$1" in
            *.tar.gz|*.tgz) tar xzf "$1" ;;
            *.tar.bz2)      tar xjf "$1" ;;
            *.tar.xz)       tar xJf "$1" ;;
            *.zip)          open "$1"    ;;   # Windows: open with Explorer
            *.7z)           open "$1"    ;;
            *)              echo "extract: unknown archive format: $1" ;;
        esac
    else
        echo "extract: '$1' is not a file"
    fi
}

# path: print PATH entries one per line, colour-coded by existence
path() {
    local IFS=:
    echo "$PATH" | tr ':' '\n' | while read -r entry; do
        if [ -d "$entry" ]; then
            echo -e "\e[32m✓  $entry\e[0m"
        else
            echo -e "\e[31m✗  $entry\e[0m"
        fi
    done
}

# take: like mkcd but more ZSH-idiomatic
take() { mkcd "$@"; }

# ── precmd hook: update RPROMPT with git status ───────────────────────────────
#
# precmd() is called by Wsh before printing the prompt.
# We query 'git' if it's on PATH; otherwise RPROMPT stays blank.
#
precmd() {
    local branch
    branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
    if [ -n "$branch" ]; then
        local dirty=""
        git diff --quiet 2>/dev/null || dirty="*"
        RPROMPT=$'\e[2;33m '"${branch}${dirty}"$'\e[0m'
    else
        RPROMPT=""
    fi
}

# ── PATH additions ────────────────────────────────────────────────────────────
# Add user-local bin directories that are conventional on Windows dev machines.
for _dir in \
    "$HOME/.local/bin" \
    "$HOME/bin" \
    "$HOME/AppData/Local/Programs/Python/Python312" \
    "$HOME/AppData/Local/Programs/Python/Python312/Scripts" \
    "$HOME/AppData/Roaming/npm" \
    "$HOME/.cargo/bin" \
    "$HOME/go/bin"; do
    if [ -d "$_dir" ] && [[ ":$PATH:" != *":$_dir:"* ]]; then
        export PATH="$_dir:$PATH"
    fi
done
unset _dir

# ── Editor ────────────────────────────────────────────────────────────────────
if command -v code >/dev/null 2>&1; then
    export EDITOR="code --wait"
    export VISUAL="code"
elif command -v notepad++ >/dev/null 2>&1; then
    export EDITOR="notepad++"
else
    export EDITOR="notepad.exe"
fi

# ── Node / Python / Rust environment ─────────────────────────────────────────
export PYTHONDONTWRITEBYTECODE=1
export PYTHONUNBUFFERED=1
export CARGO_HOME="$HOME/.cargo"
export GOPATH="$HOME/go"

# ── Local overrides ───────────────────────────────────────────────────────────
# Source a local file for machine-specific settings that should not be
# committed to version control (API keys, work-specific aliases, etc.)
if [ -f ~/.zshrc.local ]; then
    source ~/.zshrc.local
fi

# ── Startup message ───────────────────────────────────────────────────────────
echo -e "\e[1;35m  Wsh\e[0m \e[2mv1.0 — ZSH-compatible Windows terminal\e[0m"
echo -e "\e[2m  Type \e[0m\e[36mhelp\e[0m\e[2m for built-in commands, \e[0m\e[36mreload\e[0m\e[2m to re-source ~/.zshrc\e[0m"
