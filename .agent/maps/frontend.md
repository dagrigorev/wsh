# Frontend Map

## Use this map when

Use this map when the task affects visible terminal behavior, Win32 windowing, panes, keyboard input, rendering, scrollback, themes, status bar, or user interaction.

## Start

- [Context Policy](../CONTEXT_POLICY.md)
- [Project Summary](../memory/project-summary.md)
- [Known Issues](../memory/known-issues.md)
- [Inspect Task](../skills/core/inspect-task.md)

## Choose path

### Crash or wrong behavior in the window

- [Reproduce Issue](../skills/debugging/reproduce-issue.md)
- [Analyze Logs](../skills/debugging/analyze-logs.md)
- [Trace Execution](../skills/debugging/trace-execution.md)
- Start source: `src/window.cpp`, `src/main.cpp`, `src/repl.c`

### Keyboard input or hotkeys

- [Trace Execution](../skills/debugging/trace-execution.md)
- [Fix Bug](../skills/coding/fix-bug.md)
- Start source: `src/platform/input.*`, `src/window.cpp`, `src/repl.c`

### Rendering, VT, screen buffer, scrollback

- [Reproduce Issue](../skills/debugging/reproduce-issue.md)
- [Fix Bug](../skills/coding/fix-bug.md)
- Start source: `src/terminal/screen.*`, `src/terminal/vt_parser.*`, `src/terminal/layout.*`, `src/terminal/renderer.*`, `src/terminal/font.*`

### Themes or visual polish

- [Implement Feature](../skills/coding/implement-feature.md)
- [Manual QA](../skills/testing/manual-qa.md)
- Start source: `themes`, `src/terminal/renderer.*`, `src/platform/config.*`

## Finish

- [Manual QA](../skills/testing/manual-qa.md)
- [Regression Check](../skills/testing/regression-check.md)
- [Write Handoff](../skills/core/write-handoff.md)
