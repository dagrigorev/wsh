# Refactor Code

## Purpose

Improve structure while preserving behavior.

## When to use

Use when the requested outcome is cleanup, decomposition, or boundary improvement.

## Inputs

- Refactoring goal
- Files/modules involved
- Current tests
- Behavior to preserve

## Read first

- [Refactoring Map](../../maps/refactoring.md)
- [Known Decisions](../../memory/known-decisions.md)
- [Conventions](../../memory/conventions.md)

## Steps

1. Identify behavior that must stay unchanged.
2. Add characterization test if risk is high.
3. Change one subsystem at a time.
4. Avoid formatting-only noise.
5. Run regression tests.
6. Document any changed boundary.

## Output

- Behavior-preserving diff
- Verification result
- Decision note if boundary changed

## Verification

Relevant tests pass and manual QA is recorded if UI/runtime behavior might be affected.

## Next skills

- [Preserve Compatibility](../architecture/preserve-compatibility.md)
- [Regression Check](../testing/regression-check.md)
- [Write Handoff](../core/write-handoff.md)

## Anti-patterns

- Do not combine feature work with refactoring.
- Do not move legacy duplicate files casually.
- Do not change public command output unless requested.
