# ZSH → WSH porting notes

This document records the practical zsh concepts imported into WSH and the
places in the WSH codebase where they belong. It is intentionally focused on
portable shell semantics, not on copying zsh internals verbatim.

## zsh source areas reviewed

| zsh area | Main files | WSH target |
|---|---|---|
| lexical analysis | `Src/lex.c`, `Src/zsh.h` | `src/shell/lexer.c`, `src/shell/lexer.h` |
| recursive parser / AST | `Src/parse.c`, `Src/text.c` | `src/shell/parser.c`, `src/shell/parser.h` |
| command execution | `Src/exec.c`, `Src/jobs.c` | `src/shell/executor.c`, `src/shell/jobs.c` |
| builtins / command lookup | `Src/builtin.c`, `Src/hashtable.c` | `src/shell/builtins.c`, `src/shell/shell_ctx.c` |
| parameters / expansion | `Src/params.c`, `Src/subst.c`, `Src/glob.c` | `src/shell/expand.c`, `src/shell/env.c` |
| options / startup config | `Src/options.c`, `StartupFiles/zshrc` | `src/shell/builtins.c`, `config/.zshrc` |
| prompt expansion | `Src/prompt.c`, `Test/D01prompt.ztst` | `src/shell/shell_ctx.c`, `tests/test_prompt.c` |

## Ported in this pass

1. Alias execution safety, including zsh-like suppression of immediate
   self-recursive aliases. This fixes the previous `alias ls="ls"` infinite
   recursion path.
2. `command` builtin semantics for bypassing aliases.
3. Dedicated nested execution depth tracking for `shell_exec_line()` so alias,
   `eval`, prompt hooks and command substitution do not reset the active AST
   arena while an outer command list is still executing.
4. Safe `source file; next-command` behavior by preventing `source` from
   resetting the active arena while it is inside an already parsed command list.
5. Persistent function AST protection: function bodies parsed from `.zshrc` are
   no longer invalidated by the next top-level command. A future step should
   replace this with a dedicated persistent function arena or AST deep-clone.
6. zsh-compatible practical builtins: `print`, `whence`, `where`, `unsetopt`.
7. Broader option handling for `setopt`/`unsetopt`: `AUTO_CD`, `GLOB_STAR`,
   `HIST_IGNORE_DUPS`, `SHARE_HISTORY`, `NO_CLOBBER`, `ERR_EXIT`, `XTRACE`,
   `NOUNSET`.
8. Prompt escapes extended with `%/`, `%d`, `%c`, `%C`, `%T`, `%*`, `%w` in
   addition to the existing `%~`, `%n`, `%m`, `%?`, `%#`, `%j`, `%D`, `%t`.
9. File descriptor redirection parsing for `2>file`, `2>>file`, and common
   descriptor duplication forms such as `2>&1`. This is needed for zsh-style
   config snippets like `git rev-parse --abbrev-ref HEAD 2>/dev/null`.
10. Default runtime config cleaned up to avoid self-recursive `alias ls="ls"`.

## Compatibility approach

WSH should keep the current modular architecture instead of importing zsh's
large global-state implementation. The useful zsh concepts are mapped as:

- zsh lexer state → explicit `Lexer` with arena-backed token text;
- zsh parse tree → explicit `ASTNode` tagged union;
- zsh exec state → `ShellContext` with small guard fields;
- zsh option table → small option mapper in `builtins.c` for currently
  supported options;
- zsh prompt expansion → `shell_expand_prompt()` escape dispatcher;
- zsh command lookup → builtin table + `shell_which()` + `command` bypass.

## Next safe porting steps

1. Add parser support for `[[ ... ]]`, `select`, `repeat`, and `coproc` only
   after each has focused tests.
2. Replace the ad-hoc glob engine with a zsh-inspired matcher that supports
   qualifiers and `EXTENDED_GLOB` behind an option flag.
3. Add a command hash table similar to zsh `cmdnamtab`, but keep a slow path
   fallback to `SearchPathW`/dist lookup.
4. Expand `typeset` metadata so integer/export/readonly flags are stored in
   `EnvVar`, not only applied at assignment time.
5. Implement prompt conditionals such as `%(?..true.false)` after the prompt
   test suite is extended.
