# Write Handoff

## Purpose

Leave enough context for the next local agent to continue without rereading everything.

## When to use

Use at the end of every non-trivial task.

## Inputs

- Task
- Changed files
- Commands run
- Verification result
- Open questions

## Read first

- [Handoff Template](../../HANDOFF_TEMPLATE.md)
- [Known Issues](../../memory/known-issues.md)
- [Known Decisions](../../memory/known-decisions.md)

## Steps

1. Summarize the task and outcome.
2. List changed files.
3. List commands and results.
4. State what worked and what did not.
5. Record next recommended skill.
6. Update known issues/decisions if durable.

## Output

- Short handoff
- Updated memory when needed

## Verification

Another agent can continue from the handoff without scanning the full repository.

## Next skills

- [Router](../../ROUTER.md)
- [Task Template](../../TASK_TEMPLATE.md)

## Anti-patterns

- Do not omit failed verification.
- Do not include huge logs.
- Do not invent unresolved facts.
