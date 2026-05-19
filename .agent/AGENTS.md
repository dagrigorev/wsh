# Agents

Use this file to orient local agents before selecting a role.

## Local model constraints

The project is intended to work with local Ollama models through Open Code.

Assume limited context size.

Therefore:

- Prefer navigation files over large global context.
- Read only the files needed for the current skill.
- Summarize findings before moving deeper.
- Avoid loading generated files, build directories, dependency folders, binaries, logs, and large artifacts unless directly needed.
- Use `.agent/memory/commands.md` instead of rediscovering commands every time.
- Use `.agent/memory/project-summary.md` before scanning the whole repository.

## Multi-agent handoff

Every agent should leave enough information for the next agent to continue without rereading everything.

Use:

- [Handoff Template](HANDOFF_TEMPLATE.md)
- [Known Decisions](memory/known-decisions.md)
- [Known Issues](memory/known-issues.md)

## Roles

- [Architect Agent](skills/agents/architect.md)
- [Implementer Agent](skills/agents/implementer.md)
- [Reviewer Agent](skills/agents/reviewer.md)
- [Tester Agent](skills/agents/tester.md)
- [Debugger Agent](skills/agents/debugger.md)
- [Documenter Agent](skills/agents/documenter.md)
- [Release Engineer Agent](skills/agents/release-engineer.md)

## Shared rules

- Use [Context Policy](CONTEXT_POLICY.md).
- Use [Router](ROUTER.md) before opening project files.
- Prefer [Before Change Checklist](checklists/before-change.md) and [After Change Checklist](checklists/after-change.md).
