# Working on CTR2WUP

## Purpose and starting context

The product is a Windows injector GUI that converts one supported, owned 3DS
revision into a playable per-game Wii U WUP. Original diagnostics and synthetic
fixtures prove components; they are not commercial compatibility or MVP acceptance.
Do not implement the GUI before the execution/conversion pipeline is demonstrated.

At the start of a session:

1. Read `STATUS.md` for the latest verified checkpoint and pending evidence.
2. Read `docs/development-plan.md`, the relevant `docs/milestones.md` gate and
   `docs/development-tracking.md`. The user requirements are in
   `docs/project-brief.md`; `docs/gui-workflow.md` defines the intended product UI.
3. Inspect `git status --short --branch`, current PRs and Project #2. Preserve
   existing user edits. Use the existing isolated checkout; no worktrees unless asked.
4. Run `make help` and `python3 -m host doctor` when tool availability is uncertain.
   In a managed cloud environment, read its runtime skill and inspect current
   network/credential availability before diagnosing access failures.

Use `docs/repository-guide.md` for the source map and reader routes. Prefer the
specific runtime contract over old development history when implementing behavior.

## Update after each meaningful change

Before pushing a checkpoint, review every affected surface:

1. **Implementation and evidence:** update the relevant runtime/tool contract,
   tests, compatibility limits and versioned metadata. Keep private inputs and
   derived analysis ignored; publish only reviewed aggregate findings.
2. **Current reader handoff:** update `STATUS.md`, `CHANGELOG.md` and affected
   README/download instructions. Keep `docs/development-plan.md`,
   `docs/current-roadblock.md` and `docs/compatibility.md` aligned; update
   `docs/first-title.md` when title evidence changes. Remove resolved requests.
3. **All affected gates:** update evidence, remaining acceptance, state and
   waiting resources in `docs/milestone-status.json` and `docs/milestones.md`.
   Assess the parent milestone and related tasks, not just the feature. Received
   input can make a gate In progress without completing or accepting it.
4. **Native issue/PR tracking:** update `docs/features.json`, the scoped issue's
   acceptance/evidence and PR references. Run trusted-main **Development
   traceability** dry-run/apply and inspect its verified hierarchy, milestone,
   Project membership and native PR links. Broad unfinished tasks stay open.
5. **Live Project and documentation tracker:** Project #2 owns Status and Waiting
   resource. Source JSON edits and merges alone do not update existing board
   values. When evidence resolves a blocker, explicitly review the affected
   transitions, preserve unrelated/later owner edits, then refresh **Project
   tracking**. Verify live fields, mirrored issue labels and the public dashboard;
   verify public docs publication separately. Record failed/queued updates rather
   than claiming they are live. Use the Actions credential for these operations.

Before merge, complete the required checks below. After merge, verify the
published handoff and delete/verify both completed topic branches. A scoped pass
never accepts a full gate without its complete reviewed exit criteria.

## Scope and evidence

- Choose a concrete feature under an existing task. Give it a native parent,
  repository milestone and Project membership, explicit acceptance criteria and
  an associated PR. Keep `docs/features.json` current; use scoped `Closes #...`
  references and `Refs #...` for unfinished broad tasks. Do not close a milestone
  because one feature passed. Project fields own live status.
- Continue independent work when physical testing is deferred. Record the exact
  pending console/input/Windows evidence instead of making it a blanket blocker.
- Keep shared CPU/services/renderer contracts reusable. Avoid title-specific
  patches or silent successful stubs. Unsupported CPU/services/GPU state must
  fail visibly and preserve the documented atomicity boundaries.
- Never claim console execution from a cross-build or host pass. Structural
  package checks do not establish authenticity or installer acceptance.
- Synthetic programs, scenes and fixtures must be original. Respect dependency
  pins, licenses and notices; preserve signature/checksum/TLS verification.
- Keep ROMs, keys, assets, translated commercial code, saves, packages, traces
  and credentials in ignored private directories. Never put them in source,
  public docs, logs or artifacts. Use `artifacts/` for generated original evidence.

## Build and validation

Run focused checks during implementation, then required `make check` (Python and
all normal/ASan/UBSan C suites) and `make prototype` (RPX/WUHB) before merge.
Automatic **Required CI** and **Documentation build** must also pass.

Toolchain setup is `bash tools/install-wiiu-sdk.sh`; the optional pinned host
lifter is `bash tools/install-translator.sh`. Host fixtures do not require the
Wii U SDK. Original compiled-homebrew/reference validators document their own
dependency pins and use fresh output directories; retain their reports and frames.
If the execution sandbox prevents LeakSanitizer under ptrace, use the supported
execution escalation. Do not disable sanitizer checks to report a pass.

Update affected contracts, compatibility limits, `STATUS.md`, `CHANGELOG.md`,
versioned metadata and milestone evidence. Keep the current plan concise; put
chronological background in `docs/development-history.md`. Validate maintained
docs with `python3 tools/build-docs-site.py` using `docs-requirements.txt`.

## Visible delivery and GitHub access

- Develop on a focused topic branch. Commit useful checkpoints and push a draft
  PR early; do not leave substantial development visible only in a local checkout.
- For Codex commits, install the opt-in hook with
  `python3 tools/codex-attribution.py install`, then use `git codex-commit`.
  Preserve existing hooks and human attribution; do not rewrite old history
  solely for attribution. See `CONTRIBUTING.md`.
- Use the **Actions management credential** through immutable trusted-main
  workflows for GitHub management. Keep its value in Actions. The connected
  integration supplies source transport, PR lifecycle and workflow dispatch/read.
  Never run arbitrary PR code with the management secret or copy it locally.
- Run the **Development traceability** workflow dry-run first, inspect its
  receipt, apply the reviewed catalog, then verify native parent/PR relationships,
  milestone assignment and Project membership. Preserve conflicting owner data.
- After every merge, delete the completed remote and local topic branch and
  verify both are absent. If a publication rejects a concurrent write, dispatch
  a fresh trusted-main run; do not force-push over another publication.
- Finish with the delivered behavior, linked issues/PRs, validation evidence and
  material remaining gates. Never substitute test counts for product acceptance.
