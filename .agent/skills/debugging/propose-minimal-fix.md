# Propose Minimal Fix

## Purpose

Turn an isolated root cause into a safe patch plan.

## When to use

Use before editing code after debugging.

## Inputs

- Root cause
- Target files
- Verification case

## Read first

- [Make Plan](../core/make-plan.md)
- [Conventions](../../memory/conventions.md)
- [Before Change Checklist](../../checklists/before-change.md)

## Steps

1. State the one-line fix idea.
2. List exact files/functions.
3. Define pre/post behavior.
4. Define tests/manual QA.
5. Call out risks and rollback path.

## Output

- Minimal fix plan
- Verification plan

## Verification

The fix plan is smaller than a rewrite and directly addresses root cause.

## Next skills

- [Fix Bug](../coding/fix-bug.md)
- [Apply Small Change](../core/apply-small-change.md)

## Anti-patterns

- Do not include unrelated cleanup.
- Do not bypass failing behavior.
- Do not disable tests to pass.
