# Workflow

## Default flow

1. Read task.
2. Read context policy.
3. Use router.
4. Select map.
5. Select skill.
6. Read minimal project files.
7. Make plan.
8. Apply small change.
9. Verify.
10. Write handoff.

## Bugfix flow

1. Use [Debugging Map](maps/debugging.md).
2. Reproduce the issue.
3. Analyze logs or observed behavior.
4. Isolate root cause.
5. Apply minimal fix.
6. Run regression check.
7. Write handoff.

## Feature flow

1. Use [Architecture Map](maps/architecture.md).
2. Clarify expected behavior.
3. Check boundaries and compatibility.
4. Implement in small steps.
5. Add or update tests.
6. Verify manually if needed.
7. Update documentation or changelog if relevant.

## Review flow

1. Use [Read Repository](skills/core/read-repository.md) only for the relevant area.
2. Check task intent and changed files.
3. Verify boundaries, tests, and risk.
4. Report actionable findings only.
5. Finish with [Review Done Checklist](checklists/review-done.md).

## Release flow

1. Use [Release Map](maps/release.md).
2. Verify build and tests.
3. Package artifacts.
4. Update changelog or notes.
5. Write handoff with exact commands and outputs.

## Links

- [Router](ROUTER.md)
- [Skills Index](skills/index.md)
- [Before Change Checklist](checklists/before-change.md)
- [After Change Checklist](checklists/after-change.md)
