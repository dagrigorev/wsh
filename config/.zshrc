# ════════════════════════════════════════════════════════════════════════════
#  ~/.zshrc  —  Wsh configuration  (refreshed automatically on each launch)
# ════════════════════════════════════════════════════════════════════════════

# ── History ───────────────────────────────────────────────────────────────────
HISTFILE=~/.wsh_history
HISTSIZE=10000

# ── Prompt ────────────────────────────────────────────────────────────────────
# Literal ESC bytes embedded — no quoting needed, works directly.
PROMPT="[1;32m%n[0m[2m@[0m[1;34m%m[0m [1;36m%~[0m [1;35m>[0m "
RPROMPT=""

# ── Aliases ───────────────────────────────────────────────────────────────────
alias ls="ls --color=auto"
alias ll="ls -lh --color=auto"
alias la="ls -lha --color=auto"
alias ..="cd .."
alias ...="cd ../.."
alias grep="grep --color=auto"
alias reload="source ~/.zshrc"
alias notepad="notepad.exe"
alias explorer="explorer.exe"

# ── Functions ─────────────────────────────────────────────────────────────────

mkcd() {
    mkdir -p "$1" && cd "$1"
}

take() {
    mkcd "$@"
}

extract() {
    if [ -f "$1" ]; then
        case "$1" in
            *.tar.gz)  tar xzf "$1" ;;
            *.tar.bz2) tar xjf "$1" ;;
            *.zip)     open "$1" ;;
            *)         echo "unknown format: $1" ;;
        esac
    else
        echo "not a file: $1"
    fi
}

# ── precmd: git branch in right prompt ───────────────────────────────────────
precmd() {
    local branch
    branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
    if [ -n "$branch" ]; then
        RPROMPT="[2;33m${branch}[0m"
    else
        RPROMPT=""
    fi
}

# ── Local overrides ───────────────────────────────────────────────────────────
if [ -f ~/.zshrc.local ]; then
    source ~/.zshrc.local
fi

# ── Startup message ───────────────────────────────────────────────────────────
echo "[1;35m Wsh[0m [2mv1.0[0m"
echo "[36mhelp[0m for commands, [36mreload[0m to re-source ~/.zshrc"
