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

A separate public **CTR-2-WUP-docs** repository can host the generated site with
GitHub Pages on the free plan while the application repository remains private.
The export bundle includes the rendered site, notices, README and a pinned Pages
workflow; it excludes application code, ROMs, keys and private test data. Initial
creation/publication still requires repository access. A generated bundle is not
proof of a published site; only link a live URL after verifying deployment.
The connected integration rejected creation of `CTR-2-WUP-docs` with HTTP 403
**Resource not accessible by integration**. The ready export can be uploaded to
a public repository created by the owner, or published after granting the
required repository creation/Pages access. The HTML guide is usable immediately.
Private-source Pages is unavailable on the user's current plan and is not enabled.

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

`tools/setup-github-project.py` produces a read-only plan by default. It resolves
the existing seven tracker issues and checks project names, field types and
access before applying changes. Once management access is available, `--apply`
creates the private **CTR2WUP Development** board and links those issues. Existing
notes, other items, option IDs and nonempty manual values are retained. The
built-in Milestone field reflects each issue's milestone; Waiting resource
explains its pending console, input or Windows evidence.

```sh
python3 tools/setup-github-project.py --output artifacts/github-project-plan.json
python3 tools/setup-github-project.py --apply --output artifacts/github-project-result.json
```

A plan or mocked test does not prove a board exists. Read the actual result's
`applied` / `live_project_verified` fields. Source synchronization currently updates
issue descriptions; manually maintained board values are preserved.

## Settings requiring additional access

The connected integration can push code, work with issues/milestones and read
Actions runs, but GitHub rejected `createProjectV2` and repository administration/
security-setting writes as **Resource not accessible by integration**. These are
GitHub access denials, not failed source checks or an automatic approval review.

The prepared desired setup is:

- Private project **CTR2WUP Development**, linked to this repository, containing
  the seven milestone issues, with Status columns Planned / In progress /
  Awaiting evidence / Accepted and fields for milestone and waiting resource.
- About description/topics reflecting early development; optional Discussions,
  delete merged topic branches and free dependency-alert settings.

Grant the GitHub connection the relevant **Projects write** and **repository
Administration/security-setting and public docs-repository/Pages write** access to finish those protected operations,
or apply them in repository/account settings.

For this cloud environment, an optional secure personal secret requirement named
`CTR2WUP_GITHUB_MANAGEMENT_TOKEN` has been saved for `api.github.com` only. The
[Projects API documentation](https://docs.github.com/en/issues/planning-and-tracking-with-projects/automating-your-project/using-the-api-to-manage-projects)
specifies a classic personal access token with `project` scope; private repository
issues/settings additionally need `repo` access. Enter the credential in environment
settings, review/save the draft and publish the environment changes. Saving the
requirement alone does not grant access or create a board. Keep the existing
connection for normal Git/issue/Actions work; use the optional credential only
for the management operations. No token value should be pasted into chat. The [desired configuration](../.github/repository-settings.json) is
concrete and reviewable; the live milestone/issues view works while board access
is pending. Repository visibility and paid entitlements have not been changed.
