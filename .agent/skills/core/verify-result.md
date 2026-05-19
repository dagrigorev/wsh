# Verify Result

## Purpose

Check that the change works and did not break nearby behavior.

## When to use

Use after any code, config, build, or docs change.

## Inputs

Commands; expected behavior; changed files

## Read first

- [Commands](../../memory/commands.md)
- [After Change](../../checklists/after-change.md)

## Steps

1. Run original failing command.
2. run nearest tests.
3. perform manual check if needed.
4. capture output.

## Output

Verification summary with command results.

## Verification

Build/test/manual result is documented.

## Next skills

- [Write Handoff](write-handoff.md)
- [Regression Check](../testing/regression-check.md)

## Anti-patterns

- Do not claim success without evidence.
