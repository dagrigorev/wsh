# Wsh Architecture & Design

## Module Map

```
WshV2/
├── src/
│   ├── core/           wsh_core  — memory, strings, logging, paths
│   │   ├── arena.{h,c}             Region allocator
│   │   ├── str_util.{h,c}          String + UTF conversion
│   │   ├── log.{h,c}               Structured logger
│   │   └── path_util.{h,c}         File system helpers
│   │
│   ├── shell/          wsh_shell — shell language engine
│   │   ├── shell_ctx.{h,c}         Context + lifecycle + IShellIO
│   │   ├── env.{h,c}               Lexical scope chain
│   │   ├── lexer.{h,c}             Tokeniser
│   │   ├── parser.{h,c}            Recursive-descent parser → AST
│   │   ├── executor.{h,c}          AST visitor / executor
│   │   ├── expand.{h,c}            Word expansion pipeline
│   │   ├── builtins.{h,c}          Built-in command dispatch table
│   │   ├── history.{h,c}           Persistent command history
│   │   ├── jobs.{h,c}              Background job control
│   │   └── completion.{h,c}        Tab completion engine
│   │
│   ├── terminal/       wsh_terminal — VT emulation + Direct2D rendering
│   │   ├── screen.{h,c}            2D cell grid + scrollback ring buffer
│   │   ├── vt_parser.{h,c}         ANSI/VT100/xterm escape state machine
│   │   ├── renderer.{h,c}          Direct2D cell painter
│   │   ├── font.{h,c}              DirectWrite font management
│   │   └── guid_init.c             INITGUID anchor (COM IIDs)
│   │
│   ├── platform/       wsh_platform — Win32 OS services
│   │   ├── config.{h,c}            TOML config loader
│   │   ├── pty.{h,c}               ConPTY bridge + reader thread
│   │   └── input.{h,c}             VK → VT sequence translation
│   │
│   ├── main.c                      WinMain + WndProc
│   ├── repl.{h,c}                  Line editor (readline emulation)
│   └── window.{h,c}                Window class + creation
│
├── tests/              Unit tests (CTest)
├── config/             Default .zshrc + Wsh.toml (installed alongside exe)
└── res/                Win32 resources (version info, manifest, icon)
```

---

## SOLID Principles Applied

### Single Responsibility (S)

| Module | One reason to change |
|--------|----------------------|
| `arena.c` | Memory allocation strategy |
| `lexer.c` | Tokenisation rules |
| `parser.c` | Grammar / AST structure |
| `executor.c` | Execution semantics |
| `expand.c` | Word expansion pipeline |
| `renderer.c` | Direct2D painting |
| `vt_parser.c` | VT/ANSI escape handling |
| `pty.c` | ConPTY lifetime + reader thread |
| `repl.c` | Readline / line editing |
| `config.c` | Config file format |

No file crosses its layer boundary.

### Open/Closed (O)

- **Built-ins**: adding a new command means appending one entry to `BUILTIN_TABLE[]` in `builtins.c`. The dispatch function `builtin_find()` never changes.
- **AST node kinds**: adding `NODE_SELECT` (ZSH `select` loop) requires:  
  1. One new `NodeKind` enum value.  
  2. One new union member in `ASTNode`.  
  3. One new `parse_select()` function.  
  4. One new `case NODE_SELECT:` in `exec_node()`.  
  Nothing existing is modified.
- **IShellIO**: new I/O backends (pipe-capture, test buffer, network socket) implement the two-method interface without touching the shell engine.

### Liskov Substitution (L)

`TerminalIO` (in `main.c`) and `BufIO` (in tests) are both concrete implementations of `IShellIO*`. Every shell API accepts `IShellIO*` and works identically regardless of which concrete type is passed. Tests prove this by running real shell commands through `BufIO` without a window.

### Interface Segregation (I)

- `IShellIO` is deliberately minimal: only `write()` and `read_line()`. It does not include resize, title-change, or clipboard — those are terminal concerns, not shell concerns.
- `builtin_find()` returns a `BuiltinFn` (a single function pointer); callers don't need to know anything else about a built-in's implementation.
- History, jobs, and completion are separate headers each with a focused API.

### Dependency Inversion (D)

```
main.c          depends on  IShellIO* (abstract)
shell_ctx.c     depends on  IShellIO* (abstract)
executor.c      depends on  IShellIO* (abstract)

main.c defines  TerminalIO  implements  IShellIO
tests define    BufIO       implements  IShellIO
```

The shell engine (`wsh_shell`) has **no dependency** on `wsh_terminal` or `wsh_platform`. It can be linked without Direct2D or ConPTY — this is what makes unit testing possible on a headless CI machine.

---

## OOP Concepts in C

### Encapsulation

- `Arena` is opaque: callers hold an `Arena*` but never access the struct fields. All mutation goes through `arena_alloc/reset/destroy`.
- `EnvVar` / `EnvScope` internals are only accessible through `env_get/set/unset`; the linked-list structure is hidden.
- `TerminalIO` in `main.c` is a private type not exported in any header.

### Inheritance (via struct embedding)

```c
// IShellIO is the "base class"
typedef struct IShellIO { void (*write)(...); int (*read_line)(...); } IShellIO;

// TerminalIO "inherits" by placing IShellIO FIRST
typedef struct { IShellIO base; HWND hwnd; VtParser *vt; ... } TerminalIO;

// Safe up-cast (pointer equality guaranteed by C standard)
TerminalIO t = {...};
IShellIO *io = (IShellIO *)&t;   // valid
```

### Polymorphism

```c
// Executor calls through the vtable — doesn't know or care which impl it is
void exec_cmd(...) {
    io_write(ctx->io, "output");    // calls base.write(...)
}

// In production: routes through VT parser → screen
// In tests:      routes into a char buffer
```

### Abstraction

- `ASTNode` abstracts the grammar: callers (executor) work with `node->kind` and union members, never with token text or source positions.
- `expand_word()` abstracts the full 9-phase POSIX expansion pipeline behind a single call.
- `completion_compute()` abstracts path walking, PATH searching, and alias/function lookups behind one call that returns a `CompletionResult`.

---

## Design Patterns Used

| Pattern | Where | Why |
|---------|-------|-----|
| **Strategy** | `IShellIO` | Swap I/O backends (terminal / test buffer / pipe) |
| **Composite** | `ASTNode` binary/wrap nodes | Arbitrary-depth command trees |
| **Command** | `ASTNode` leaf `NODE_CMD` | Encapsulates executable unit |
| **Visitor** | `exec_node()` switch over `NodeKind` | Dispatch without virtual dispatch overhead |
| **Chain of Responsibility** | `EnvScope` linked list | Lexical variable lookup |
| **Observer** | `precmd` / `preexec` hooks | Decouple prompt rendering from execution |
| **Singleton** | Logger (`g_log` in `log.c`) | One global log state, thread-safe |
| **Memento** | History navigation | Restore saved line-buffer snapshots |
| **Template Method** | Expansion pipeline in `expand_word()` | Fixed order, pluggable phases |
| **Factory** | `builtin_find()` | Return the right `BuiltinFn` by name |

---

## Self-Review: Issues & Improvements

### Known limitations

1. **Pipe execution is sequential, not concurrent.**  
   In `exec_pipe()`, the left side runs to completion before the right side starts reading.  This is correct for simple pipelines but breaks pipelines where the left side blocks waiting for the right to consume (`cat bigfile | head -5`).  
   *Fix*: run left in a separate thread or use `CreateProcessW` for both sides with inheritable pipe handles.

2. **NODE_SUBSHELL executes in the same context.**  
   True ZSH subshells fork; variable mutations are not visible to the parent.  On Windows, true forking is not available; the fix is to deep-copy the `ShellContext` (env scope chain) before executing the subshell body and discard it afterward.

3. **Command substitution uses a static capture buffer.**  
   `expand_cmd_subst()` uses a file-scope `SB s_cap_buf` which is not re-entrant.  Nested command substitution (`$(echo $(echo hi))`) will corrupt the outer capture.  
   *Fix*: pass a local `SB*` through a thread-local or stack-allocated capture context.

4. **No `SIGWINCH` equivalent for resize in PTY mode.**  
   The renderer sends `pty_resize()` on `WM_SIZE`, but the child process may not update its internal terminal size until it receives the next `TIOCSWINSZ` equivalent via `ResizePseudoConsole()`. This is already called; the race is benign.

5. **Glob `**` (recursive) not yet implemented.**  
   `expand_glob()` calls `PathMatchSpecW` which handles `*` and `?` but not recursive `**`. A recursive `FindFirstFileW` walk is needed.

### Potential issues

- `strncasecmp` is a POSIX function aliased to `_strnicmp` via `str_util.h`. This is safe on MSVC but the alias must appear before any other include that might define it.
- The `test_helpers.h` framework uses `__attribute__((constructor))` which GCC/Clang support but MSVC does not. Replace with explicit `TEST_REGISTER()` calls in `main()` for MSVC builds, or use `/INCLUDE` pragma linker tricks.
- History file is plain UTF-8 text; lines with embedded NUL bytes would be truncated. Not a practical issue for shell history.

---

## Building

```cmd
:: Open "Developer Command Prompt for VS 2022"
mkdir build && cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
ctest --output-on-failure   :: run unit tests
nmake install               :: copy to %LOCALAPPDATA%\Wsh
```

Or with Ninja (faster):

```cmd
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
ninja install
```
