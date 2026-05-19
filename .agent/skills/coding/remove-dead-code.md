# Remove Dead Code

## Purpose

Safely remove unused code or obsolete files.

## When to use

Use for cleanup after confirming code is not built or referenced.

## Inputs

- Candidate code/files
- Build graph evidence
- Search results

## Read first

- [Refactoring Map](../../maps/refactoring.md)
- [Known Decisions](../../memory/known-decisions.md)
- [Inspect Build System](../build/inspect-build-system.md)

## Steps

1. Confirm the code is not part of current CMake targets.
2. Search references.
3. Remove or isolate in a dedicated change.
4. Update docs if source layout changes.
5. Run full build and tests.

## Output

- Removed dead code
- Evidence of non-use
- Verification result

## Verification

Full build and tests pass. For legacy source cleanup, confirm active modular targets still build.

## Next skills

- [Regression Check](../testing/regression-check.md)
- [Write Handoff](../core/write-handoff.md)

## Anti-patterns

- Do not remove code based only on filename.
- Do not mix dead-code removal with feature work.
- Do not delete resources used at runtime.
