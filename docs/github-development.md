# GitHub development

GitHub is the development record: current source, reviewable changes, milestone
issues, CI logs/artifacts and versioned original diagnostic releases. Keep the
product goal and [current limits](../STATUS.md) visible; no badge establishes
commercial gameplay or physical-console acceptance.

## Roadmap and evidence

Each of the seven [milestones](https://github.com/Jdiller-86/CTR-2-WUP/milestones)
has a [tracker issue](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+label%3Amilestone).
Area and waiting labels identify CPU/runtime/graphics/host work and console,
input or Windows gates. Checked foundations and open full-acceptance items are
separate. Add test evidence in comments; maintainers update the versioned source
assessment after reviewing it.

`docs/milestone-status.json` supplies managed tracker sections. The Roadmap
synchronization workflow updates those sections on main without deleting comments
or manual notes, duplicating trackers or automatically closing acceptance gates.
To preview or explicitly apply a source update with normal `gh` authentication:

```sh
python3 tools/sync-github-progress.py
python3 tools/sync-github-progress.py --apply
```

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

The 0.17.0 kit contains Native and GX Transfers. The 0.14.6 kit remains the
Save/RomFS follow-up. [Getting started](getting-started.md) explains SD layout,
Aroma/RPX launch and report delivery.

## Agent support

[Shared instructions](../AGENTS.md), [Copilot guidance](../.github/copilot-instructions.md)
and four specialist profiles cover CPU/runtime, host conversion, graphics and
read-only console-evidence review. Use supported Copilot clients when the account
has access. Profile files do not activate a subscription or automatically assign
work to an external service. Current development also used independent parallel
runtime and workflow/documentation reviews; actual findings were corrected and
checks rerun.

## Settings requiring additional access

The connected integration can push code, work with issues/milestones and read
Actions runs, but GitHub rejected `createProjectV2` and repository administration/
security-setting writes as **Resource not accessible by integration**. These are
GitHub access denials, not failed source checks or an automatic approval review.

The prepared desired setup is:

- Private project **CTR2WUP Development**, linked to this repository, containing
  the seven milestone issues, with Status columns Planned / In progress /
  Awaiting evidence / Accepted and fields for milestone and waiting resource.
- Protect main against force pushes/deletion, require up-to-date **Required CI**
  and PR review flow. Avoid requiring a second human approver for a solo maintainer
  unless one is available. Do not enable protection before its checks actually run.
- About description/topics reflecting early development; optional Discussions,
  delete merged topic branches and free dependency-alert settings.

Grant the GitHub connection the relevant **Projects write** and **repository
Administration/security-setting** access to finish those protected operations,
or apply them in repository/account settings. No token value should be pasted
into chat. The [desired configuration](../.github/repository-settings.json) is
concrete and reviewable; the live milestone/issues view works while board access
is pending. Repository visibility and paid entitlements have not been changed.
