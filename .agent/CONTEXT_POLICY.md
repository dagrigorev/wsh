# Context Policy

## Never load everything

Do not read the whole repository unless the task explicitly requires it.

## Read in layers

1. Read the task.
2. Read `.agent/ROUTER.md`.
3. Read only the selected map.
4. Read only the selected skill.
5. Read only the files required by that skill.

## Prefer summaries

Use:

- `.agent/memory/project-summary.md`
- `.agent/memory/commands.md`
- `.agent/memory/conventions.md`

before scanning large source trees.

## Stop conditions

Stop reading more files when:

- the relevant module is identified;
- the build/test command is known;
- the cause of the issue is localized;
- the required change can be made safely.

## Handoff

At the end of work, update or create a short handoff note using:

- `.agent/HANDOFF_TEMPLATE.md`

## Avoid by default

- Build directories
- Dependency folders
- Generated files
- Large logs
- Binary artifacts
- IDE caches

See also [Agents](AGENTS.md), [Workflow](WORKFLOW.md), and [Commands](memory/commands.md).
