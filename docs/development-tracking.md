# Tracking development from the repository

The [Development Project](https://github.com/users/Jdiller-86/projects/2) is the
working board. The seven [milestone checklists](milestones.md) define full product
acceptance. Every implementation has a scoped feature issue, a native parent,
a repository milestone and an associated pull request.

## Follow an implementation

1. Open a milestone overview, then its native sub-issues.
2. Open the relevant work task, then its scoped feature children.
3. Read the feature's scope, acceptance criteria and evidence limits.
4. Open **Development / Linked pull requests** to inspect the actual change,
   checks, commits and branch. PRs also appear in the Project's Pull requests view.

Text references alone do not establish native links. The management workflow
creates and verifies GitHub parent relationships and closing PR references.
Historical PRs have retrospective feature issues; those issues describe bounded
delivered checkpoints rather than implying the whole parent task is complete.

## Contributor workflow

Create a scoped feature under an existing task before starting implementation.
Assign its M1–M7 repository milestone and add it to Project #2. Give it concrete
scope, reproducible acceptance checks and explicit limits. Add its stable identity
and PR relationship to the reviewed feature catalog, `docs/features.json`.

Work on a focused branch, commit attributable checkpoints and push a draft PR
early. Link the scoped issue with `Closes #...`; reference broader unfinished
tasks with `Refs #...`. Keep the PR in the same milestone and Project. After
required checks and review, merge and delete both remote and local topic branches.

Run the trusted-main management dry-run and apply after opening the draft. For an
already planned feature, the tool can discover its new PR through the native
closing reference even while the catalog edit is on the topic branch. It reads
GitHub relationships and never executes that branch. An ambiguous relationship,
unknown feature or active PR extending a closed feature fails before writes.

**Done** means the scoped acceptance criteria were met. It does not mean a parent
task, console route or full milestone was accepted. Full gates need their entire
documented checklist and reviewed evidence. Physical testing remains pending
when only host checks or RPX/WUHB builds exist.

## Development without hosted Actions

Use the owner-authorized [direct management and local validation route](direct-development.md)
when hosted jobs cannot start. It uses the independently supplied management
identity, preflights fixed targets and requires clean current main for apply.
Prepare reviewed status/resource transitions in `docs/project-reviews.json`;
completed review markers preserve later owner edits. Record pending live updates
when access is missing instead of claiming source edits updated the board.

## Reviewed management and audit

Run **Development traceability** on `main` with `apply=false` for a read-only
preflight, then `apply=true` for the reviewed catalog. Both runs use an immutable
trusted-main checkout; the management credential remains an Actions secret.
No arbitrary branch, issue body or workflow input is executed with that credential.

The tool backfills native parents for the 35 established tasks, creates bounded
feature issues, links PRs, assigns milestones and ensures Project membership.
It fails before writes on conflicting native parents or a historical feature
whose PR has not merged. Its private artifact records verified live relationships
and any partial operation failure; a failed run must be reviewed before retrying.

The initial status migration only replaces old empty/Todo values. Later manual
Project status edits remain authoritative. Merged scoped features become Done;
no full gate becomes Accepted through this workflow. When hosted execution is available, the hourly **Project
tracking** workflow publishes safe aggregates separately and does not expose
private issue titles, bodies or feature identifiers in the public dashboard.

## Next renderer features

Scoped shader-capacity issue #97 has verified native parent #28 and M4 assignment;
draft PR #98 also has M4. Its 512-word implementation and original probes do not
accept #28/M4. Project membership/catalog reconciliation remains pending because
the new environment's named credential still returns `Resource not accessible
by integration` for personal Project #2. No live status or dashboard update is
claimed; the reviewed Mario transitions remain queued.

| Feature | Parent task | Acceptance focus |
| --- | --- | --- |
| Compiled original guest draws | #27 | Standard GSP queue, exact targets, rollback and retained state |
| Vertex shader arithmetic | #28 | Original numeric references and unsupported-state rejection |
| TEV fragment combinations | #29 | Independent fragment reference for supported operations |
| Texture sampling | #29 | Checked owned descriptors, wrap/filter rules and exact scenes |
| Resource coherency | #30 | Memory generations, invalidation and readback correctness |
| GX2 shader translation | #28 | Native ABI, explicit shader misses and comparison route |
| Physical scene comparison | #31 | Exact-build Wii U results, repeat launch/exit and measured costs |

These are independently reviewable features. Completing a software draw path
does not establish general PICA compatibility or native GPU translation.


The v0.23 command review completes only task #27 after its two criteria are
verified against merged PR #48/#57 and original reference traces. Its review
marker is recorded once; later maintainer reopen/status changes are preserved.
No full milestone or other unreviewed task can complete through that rule.
