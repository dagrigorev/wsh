# Code Review Prompt

You are working locally in this repository through Open Code.

Use the local agent system:

1. Read `.agent/CONTEXT_POLICY.md`.
2. Read `.agent/ROUTER.md`.
3. Select the smallest relevant map.
4. Use the most relevant skill.
5. Read only needed files.
6. Verify the result.
7. Write a short handoff.

Do not load unnecessary files.
Do not rewrite unrelated code.
Do not invent missing project facts.
Use `.agent/memory/commands.md` for known commands.

## Focus

Review changed files for correctness, boundaries, tests, and risk. Do not rewrite unless requested.

## Related

- [Router](../ROUTER.md)
- [Workflow](../WORKFLOW.md)
- [Skills Index](../skills/index.md)
