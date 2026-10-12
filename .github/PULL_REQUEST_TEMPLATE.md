## Change

Explain the problem and resulting behavior. Link the issue or milestone this advances.
Link the focused work issue and its parent milestone. Use `Closes #...` only when
that issue's acceptance criteria are met; milestone overviews require their full
reviewed exit criteria.

Type: bug fix / feature / documentation / tooling / refactor.

## Validation

Record commands and outcomes. Separate desktop tests, PowerPC cross-builds, reference comparisons, Windows execution and physical-console results. Mark unavailable checks as pending.

Describe how a reviewer can reproduce the relevant behavior, including the build
or release version and expected result. Record an actual execution check or the
precise missing resource. For rendering/UI changes, include a screenshot, original
reference-frame comparison or guided visual result appropriate to the change.
Console TXT observations remain valid evidence; photographs are not mandatory.

## Boundaries

State unsupported behavior, changed contracts and any remaining acceptance gates.

- [ ] Required checks completed: `make check` and `make prototype`, or a specific unavailable prerequisite is recorded above.
- [ ] Relevant status, milestone, compatibility and build documentation reflect the evidence.
- [ ] Fixtures are original; changes and artifacts contain no ROMs, keys, commercial assets, saves, translated commercial code or credentials.
- [ ] Dependency licenses, pinned versions and notices were reviewed where dependencies changed.
- [ ] Runtime and visual evidence are recorded, with unavailable checks left pending.
- [ ] The PR targets `main` from a focused topic branch; `Required CI` and `Documentation build` have passed before merge.
