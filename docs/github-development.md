# GitHub development

[Project #2](https://github.com/users/Jdiller-86/projects/2) is the authority for
status, waiting resources and reviewed acceptance. GitHub also records source, reviewable changes, milestone
issues, CI logs/artifacts and versioned original diagnostic releases. Keep the
product goal and [current limits](../STATUS.md) visible; no badge establishes
commercial gameplay or physical-console acceptance.

## Roadmap and evidence

Each of the seven [milestones](https://github.com/Jdiller-86/CTR-2-WUP/milestones)
has an [overview tracker](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+label%3Amilestone)
and five [focused work issues](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+label%3Awork-item).
The 35 work issues carry acceptance criteria, prerequisite links and required
evidence; the seven overviews coordinate full milestone acceptance.
Area and waiting labels identify CPU/runtime/graphics/host work and console,
input or Windows gates. Checked foundations and open full-acceptance items are
separate. Add test evidence in comments; maintainers update the versioned source
assessment after reviewing it.

`docs/milestone-status.json` supplies versioned requirements and evidence for
managed milestone descriptions, overviews and work-issue sections. Its state
fields seed empty values at initial setup; existing Project states and waiting
resources always win. The Project tracking workflow updates evidence sections
on trusted main without deleting comments
or manual notes, duplicating trackers or automatically closing acceptance gates.
To preview or explicitly apply a source update with normal `gh` authentication:

```sh
python3 tools/sync-github-progress.py
python3 tools/sync-github-progress.py --apply
```

## Labels and branch status

| Labels | Meaning |
|---|---|
| `status:planned` | Defined work awaiting implementation or triage. |
| `status:in-progress` | Active/partial work, a draft PR or checks still pending. |
| `status:blocked` | Missing prerequisites or current failed checks. |
| `status:awaiting-evidence` | Scoped implementation awaits required evidence; also an explicit PR hold. |
| `status:ready-for-review` | Current PR head has passing Required CI and Documentation checks; maintainer review remains. |
| `status:done` | A Project task was reviewed as Done, an unmanaged task completed, or a PR merged. |
| `status:accepted` | A Project overview was explicitly Accepted after full-gate evidence review; never a task or merged PR. |
| `status:closed` | A task was abandoned or a PR closed without merging. |
| `area:*`, `documentation`, `bug`, `enhancement` | Component and kind of work. |
| `needs-console`, `needs-input`, `needs-windows` | Specific waiting resources. |
| `p0` / `p1` / `p2` / `p3` | Emergency / urgent / normal / low triage priority. |

Managed roadmap issues mirror Project Status and recognized Waiting resource
categories. Edit the board fields to change them; an issue closure or manual
label does not change canonical Project state. PRs and unmanaged issues retain
their lifecycle/check-based status automation. Other labels, priorities and
manual notes are preserved. Closing a task or merging code never accepts a full milestone.
Use `status:awaiting-evidence` to hold a PR for missing validation; CI does not
remove that hold. Record runtime and visual evidence in the issue/PR, using
original reference frames, browser captures or guided console TXT observations.

The **Work-item statuses** workflow responds to issue/PR lifecycle, deliberate
status-label changes and completed CI/Documentation runs. It skips Project-managed
roadmap issues and leaves their mirrors to Project tracking. It executes trusted
immutable main code and reads API data; it never executes a contributor's branch
or downloads untrusted build artifacts. Status writes touch only managed status
labels. Current merge checks must include the actual PR head, so old results
cannot mark newly pushed code ready. The Actions summary/artifact reports branch
names, associated PRs, states, checks and reasons.

```sh
python3 tools/sync-github-status.py --output artifacts/github-status.json
python3 tools/sync-github-status.py --number 12 --status awaiting_evidence --apply
```

GitHub labels apply to issues and PRs. Use focused names such as
`feature/m4-pica-commands`, `fix/m1-result-write` or `docs/testing-guide`; their
status is visible through the linked PR, CI checks and the workflow report.
Release Git tags identify versions and remain separate from task status.

The structure adapts [Odysseus's coordination milestone](https://github.com/odysseus-dev/odysseus/milestone/1),
[label taxonomy](https://github.com/odysseus-dev/odysseus/labels) and
[review template](https://github.com/odysseus-dev/odysseus/blob/dev/.github/pull_request_template.md):
focused tasks, explicit blockers/evidence, triage priority and reproducible review.
CTR2WUP's acceptance criteria remain specific to Wii U execution and the planned
injector; unavailable console/game tests stay pending. The user's GitHub Free,
private-source and no-branch-protection choices remain in force.

## Pull requests and CI

Use a topic branch and the PR template for reviewable problem/behavior/validation
changes. CODEOWNERS identifies the maintainer. **Required CI** summarizes:

- workflow lint with checksum-pinned actionlint;
- Python 3.12 and 3.14 fixtures and installed-wheel smoke checks;
- GCC and Clang portable tests with ASan/UBSan;
- native Windows host and normal portable C fixtures;
- Wii U RPX/WUHB build using the official immutable devkitPro SDK image.

Actions are pinned by commit, permissions are scoped per workflow/job, fork PRs
receive no repository write access, logs survive failures and concurrency avoids
unneeded duplicate PR runs. Test/build artifacts include the source commit;
Wii U artifacts include checksums, provenance and notices. Dependency update PRs
cover actions and Python dependencies. Review them; they are not auto-merged.

The separate manual Windows workflow validates real external AOT/Unicode execution
and WPF tooling. CodeQL is manual-only and requires confirming existing scanning
eligibility. This setup does not buy or enable Advanced Security. Read each run's
actual result; workflow publication is not proof that hosted checks succeeded.

## Releases

Development releases are prereleases with explicit diagnostic scope, source tag,
notes, checksums and matching source/wheel/console artifacts. Do not call an RPX/
WUHB a WUP or label a diagnostic release playable. No real game, key, seed DB,
save, private trace or extracted commercial asset belongs in an Actions artifact
or release. Preserve older requested test kits and their versioned instructions.

The 0.17.0 kit contains Native and GX Transfers. Its release also includes a
single offline HTML guide and generated public docs-only site bundle. The 0.14.6 kit remains the
Save/RomFS follow-up. [Getting started](getting-started.md) explains SD layout,
Aroma/RPX launch and report delivery.

The **Publish development diagnostics** workflow is dispatched explicitly from
main with the version in `pyproject.toml`. It runs required checks, verifies SDK/
lifter/libctru pins, executes the original GX application with sanitizers, builds
RPX/WUHB and smoke-tests the wheel. The packager checks reported binaries/frames,
source and wheel module equality, ZIP CRCs and SHA-256 sums. A separate job with
`contents: write` publishes the matching prerelease with GitHub's native token;
a failed upload leaves a draft. Published versions cannot be replaced by different
artifacts. This route also avoids the cloud connection's rejected upload-host
authentication. It does not close console or commercial acceptance gates.

## Agent support

[Shared instructions](../AGENTS.md), [Copilot guidance](../.github/copilot-instructions.md)
and four specialist profiles cover CPU/runtime, host conversion, graphics and
read-only console-evidence review. Use supported Copilot clients when the account
has access. Profile files do not activate a subscription or automatically assign
work to an external service. Current development also used independent parallel
runtime and workflow/documentation reviews; actual findings were corrected and
checks rerun.

Codex work uses the repository's opt-in [commit attribution hook](../CONTRIBUTING.md#codex-commit-attribution).
Its verified public bot author and standard Codex co-author trailer provide
visible credit while normal Git/PR authentication continues through the connected
account. It does not impersonate a bot login or rewrite earlier history.

## Documentation and free hosting

The Markdown guides are readable directly in GitHub. Each Documentation workflow
build also provides a searchable Material site and **a single HTML guide that
opens directly in a browser**, with navigation, search and a print layout. The
HTML guide requires no account, server or internet connection to read its content.
Download it from a matching development release or Documentation run artifact.

The public [CTR2WUP documentation site](https://jdiller-86.github.io/CTR-2-WUP-docs/)
is deployed on free GitHub Pages while application source stays private. The
export contains rendered guides, notices, provenance and a pinned Pages workflow;
application code, ROMs, keys and private test data are excluded. Publication and
Pages Actions succeeded, and HTTP checks verify the site, dashboard, JSON and
chart. Private-source Pages is not used. The offline reader remains available.

```sh
python3 -m venv artifacts/docs-build-venv
artifacts/docs-build-venv/bin/python -m pip install -r docs-requirements.txt
artifacts/docs-build-venv/bin/python tools/build-docs-site.py
artifacts/docs-build-venv/bin/python tools/package-docs-site.py
```

The strict build validates rendered links, anchors, search and assets. The site is
in `artifacts/docs-site/site/`; the HTML guide and public-repository bundle are
in `artifacts/docs-publication/`. Their JSON receipts and checksums identify the
source commit. Source links stay commit-specific and require repository access.
The site and HTML guide preserve the product goal, progress and acceptance limits.

## Maintenance on GitHub Free

The private source repository uses topic branches and reviewable pull requests.
Check the latest **Required CI** and **Documentation build** on the PR before
merging; keep source and evidence together. Preserve history and use a new
version for changed published artifacts. Dependencies/actions are pinned,
CODEOWNERS identifies the maintainer, and contributor/security/result guides
provide the review context.

The user explicitly declined branch protection because the repository is private
on GitHub Free. No protection rule or administration setup tool is incorporated.
These maintenance practices are team conventions; GitHub does not enforce them.

## Board setup

The owner-selected [Project #2](https://github.com/users/Jdiller-86/projects/2)
is the tracking source of truth. Setup resolves that exact board and all 42
managed issues. It fails before writes on inaccessible/ambiguous data and never
creates a replacement board. Existing title, notes, option IDs, manual items,
nonempty field values and existing view filters/layouts are preserved.

The detailed overview explains the seven deliverables and acceptance boundaries,
status policy, private evidence and next console handoff. Work type and Priority
supplement Status, Milestone and Waiting resource. New filtered views cover
Delivery gates, Implementation work, Evidence handoff and Pull requests. Source
requirements initialize empty fields only; Accepted is reserved for reviewed full
milestone overviews. See the [overview template](github-project-overview.md).

```sh
python3 tools/flesh-github-project.py --output artifacts/github-project-plan.json
python3 tools/flesh-github-project.py --apply --output artifacts/github-project-result.json
```

`applied`, `live_project_verified`, completed operations and partial-change flags
state what actually happened. A denied read cannot establish a live board. As of
this source increment the personal secret requirement is saved, but the environment
reports `has_saved_binding:false`; its Project read still uses the limited
integration and returns **Resource not accessible by integration**.

## Project-backed progress

[Project #2](https://github.com/users/Jdiller-86/projects/2) is the authority for
Status, Waiting resource and full-gate acceptance. Its seven overview issues and
35 focused tasks, detailed overview, work-type/priority fields and filtered views
are populated and verified through the repository Actions secret. Existing owner
values and notes are preserved. GitHub's default Todo option is shown as Planned
in the aggregate without changing the option or its ID.

The tracking workflow reads Project fields hourly and on request, then publishes
an allowlisted JSON snapshot, stacked milestone chart and interactive dashboard.
Bars show five work items per milestone; completed children never automatically
accept a full gate. Chart timestamps are visible; the dashboard marks snapshots
older than two hours stale and preserves the previous update time on read errors.
GitHub can cache README images; open the dashboard for current freshness.

The public docs publisher runs reviewed immutable main source, requires a strict
searchable documentation build and verifies the docs-only archive before any
write. It can explicitly create only `Jdiller-86/CTR-2-WUP-docs` as public, configure
free workflow-based Pages and publish its generated site. Application visibility
never changes. Both publishers preserve existing tree contents and update main
without force. Progress lives in `site/progress/`, inside the Pages upload root.
[The documentation site](https://jdiller-86.github.io/CTR-2-WUP-docs/) and
[interactive dashboard](https://jdiller-86.github.io/CTR-2-WUP-docs/progress/) are
live. Their HTML, aggregate JSON and chart return HTTP 200; publication and Pages
Actions succeeded. The README embeds the generated chart.

Only seven documented milestone names, aggregate counts, accepted-gate counts,
fixed waiting-resource categories, freshness and the Project link enter the public
snapshot. No private issue title/body/number, item ID, owner note, ROM, key or trace
is exported. Unknown custom status/resource values remain in fixed Other buckets.

Configure `CTR2WUP_GITHUB_MANAGEMENT_TOKEN` in repository Actions secrets with
Project and the required repository/Pages permissions. Cloud-environment secrets
are separate; the Actions route is verified, while cloud Project calls still use
a limited integration. Tokens are never requested in chat or copied into reports.
Use trusted-main **Project tracking** for setup/export and **Publish public
documentation** for docs-only publication. Neither runs untrusted PR code with
the management token. Dry-run tools are available for reviewing mutations.

## Repository settings and authentication

Normal Git, issues, milestones, PRs and Actions use the connected integration.
Its direct cloud Project/administration calls remain limited. Trusted-main
management workflows use the separate repository Actions secret
`CTR2WUP_GITHUB_MANAGEMENT_TOKEN`; this route has populated Project #2 and
published the docs-only Pages site. No further token entry is needed for those
verified operations.

**Repository features** applies the requested About/documentation URL, topics,
Discussions, merged-branch cleanup and free Dependabot alerts, then verifies the
result through the API. **Project tracking** owns board setup, explicit reviewed
task transitions and public aggregates. **Publish public documentation** verifies
the docs-only export before publication. Each preserves operation receipts.
The [desired settings](../.github/repository-settings.json) specify mutation scope;
actual API receipts establish which operations succeeded.

Only trusted immutable main code executes with the management token. Values are
consumed normally by the GitHub CLI and never copied into reports, source or
chat. Existing owner board values remain authoritative; source synchronization
cannot overwrite them. No branch protection or paid feature is enabled, and
application repository visibility remains private.
