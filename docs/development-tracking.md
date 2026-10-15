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
whose PR has not merged. Its public-safe artifact records verified live relationships
and any partial operation failure; a failed run must be reviewed before retrying.

The initial status migration only replaces old empty/Todo values. Later manual
Project status edits remain authoritative. Merged scoped features become Done;
no full gate becomes Accepted through this workflow. When hosted execution is available, the hourly **Project
tracking** workflow publishes safe aggregates separately and does not expose
private issue titles, bodies or feature identifiers in the public dashboard.

The source is public and hosted validation is available. Scoped #99/PR #100
consolidates publication and uses the existing Actions management secret for
reviewed tracking operations. Project read/write access, the bounded Mario
transition and label/dashboard refresh have verified Actions receipts. Native
reconciliation receipts are linked in [current status](../STATUS.md). The
environment binding's earlier failure does not block Actions.

Scoped shader-capacity issue #97 has verified native parent #28 and M4 assignment;
merged PR #98 also has M4 and passing hosted Required CI and Documentation.
Its 512-word implementation and original probes do not accept #28/M4. Native
parents/PR links, milestones and Project membership verify in
[reconciliation run 38608010840](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38608010840). The reviewed Mario transitions
and label/dashboard refresh also verify; console/full gate criteria remain open.

## Next renderer features

The focused hardware round is scoped #87 under #20/M2, with PR #106 and
references to #13/#28/#31/#81 for fresh launch, arithmetic and picture/timing
evidence. Its exact tested build now has all requested reports/traces: two
arithmetic launches, correctly ordered picture confirmation, Guest Draws Plus
repeat/all three guide passes and all five exits through `main_return`. Version 0.25.0 extends the same #87/#106 scope with exact changed-span
scaling and row publication for that measured cost. Independent full-pixel,
failure and original workload checks pass; new physical correctness/speed
remain pending. PR #106 merged on October 15 after all required hosted checks
passed; its completed local/remote branch is removed. No files or reruns remain
requested for the completed 0.24.1 round.

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

Scoped #76 has PR #105 for exact ADD/MUL. Original numeric/scene/queue
checks pass; required full/platform checks gate delivery through that PR. Trusted-main reconciliation
[38625450461](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38625450461)
verifies 69 native parents, 29 PR links and all 105 Project members. #28/M4 stay open.


The v0.23 command review completes only task #27 after its two criteria are
verified against merged PR #48/#57 and original reference traces. Its review
marker is recorded once; later maintainer reopen/status changes are preserved.
No full milestone or other unreviewed task can complete through that rule.

Scoped #103 under #25/M3 implements the original DSP ADPCM decoding foundation.
Its codec/rational/seek/failure checks do not close broad #25. The reviewed
`dsp-adpcm-foundation-v025` transition activates only `m3-guest-audio`, preserving
the console wait and owner notes. It cannot accept a full gate.

Scoped #110 hardens membership readback after an acknowledged PR #108 addition
remained absent from the paginated connection. For at most eight missing public
references, the reconciler checks the issue/PR's independent `projectItems`
edge, including archived state, exact content and selected Project identities.
It counts only independently confirmed active members, preserves both readbacks
and never unarchives or changes Project settings. Archived/ambiguous identities,
genuine absence, permission errors and read budgets still fail visibly.

The #110/PR #111 live check is complete: [dry-run 38752356368](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38752356368)
was inspected before [apply 38752814566](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38752814566).
At that checkpoint, 71 native parents, 33 PR links and 111/111 Project members
verify with no untracked PRs. PR #109/#111 are confirmed through exact independent
active links while the Project connection still lags. Final documentation PRs
are reconciled separately; the live board/dashboard remain authoritative.
