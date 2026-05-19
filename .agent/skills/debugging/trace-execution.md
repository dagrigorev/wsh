# Trace Execution

## Purpose

Follow the failing path through minimal relevant code.

## When to use

Use when the root cause is not obvious from logs.

## Inputs

Task; relevant files; constraints

## Read first

- [Context_Policy](../../CONTEXT_POLICY.md)
- [Project Summary](../../memory/project-summary.md)
- [Commands](../../memory/commands.md)

## Steps

1. Read minimal context.
2. inspect relevant files.
3. make a small plan.
4. apply focused work.
5. verify.
6. document result.

## Output

Trace Execution result with short explanation and links.

## Verification

Run the nearest relevant build, test, or manual check.

## Next skills

- [Isolate Root Cause](isolate-root-cause.md)
- [Propose Minimal Fix](propose-minimal-fix.md)

## Anti-patterns

- Do not make unrelated changes
- Do not invent project facts
- Do not skip verification.
