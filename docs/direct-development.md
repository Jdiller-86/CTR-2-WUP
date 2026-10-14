# Development without hosted Actions

The owner authorizes direct GitHub API management and scoped local merge evidence
when hosted jobs cannot start. Existing workflows remain available. This route
does not change billing, spending limits, source visibility or branch protection.

## Secure credential setup

In the cloud environment's secure secret settings, bind
`CTR2WUP_GITHUB_MANAGEMENT_TOKEN` for `api.github.com`. Use the same management
identity as Actions, independently supplied by the owner. If its token value is
unavailable, create a classic personal access token for **Jdiller-86** with
**repo** and **project** scopes. These cover the private source, personal Project
#2 and authorized public docs repository. Actions secrets cannot be read back.
Never paste a token in chat, arguments, logs, source or receipts, or retrieve it
from Actions. The direct tool never silently selects another credential.

```sh
python3 tools/manage-github.py preflight
```

Preflight checks fixed repository identities/write permission and reads all 42
managed Project items/fields before mutations. Project read access alone does
not prove mutation access: supply read/write Project scope. A ready binding or
successful issue write is insufficient. Current Project requests return 403;
the ignored private access receipt identifies the failing stage without secrets.

## Review and apply

Every `--apply` operation requires clean **main at its current remote commit**.
Prepare dry-runs on the reviewed topic branch; inspect receipts before applying.
The command runs fixed scripts without arbitrary PR code or issue instructions.

```sh
python3 tools/manage-github.py traceability
python3 tools/manage-github.py traceability --apply
python3 tools/manage-github.py evidence
python3 tools/manage-github.py evidence --apply
python3 tools/manage-github.py review --review mario-input-received-v023
python3 tools/manage-github.py review --review mario-input-received-v023 --apply
python3 tools/manage-github.py progress
python3 tools/manage-github.py progress --apply
```

Traceability verifies native parents, milestones, Project membership and PR
links. Evidence refresh changes managed descriptions only. The private
`docs/project-reviews.json` review activates only M5/intake/requirements and
removes the resolved exact owned-input resource from six M5 items. Console waits
and manual notes survive. Done, Closed and Accepted cannot be set by this tool.
All targets preflight before writes; exact identity/fields are checked before
and after each update. Only verified completion records a plan-bound comment in
the overview. Replay preserves later owner edits; recorded plans are immutable.

GitHub does not provide a transaction across Project field updates. Serialize
management runs, inspect partial-operation receipts after failures and review
live fields before retrying. Progress mirrors labels and publishes only
allowlisted live aggregates; source states never substitute for Project values.
Public snapshots become stale after two hours. Refresh after meaningful changes.

Documentation publication is separate and uses the existing free public Pages
deployment, without private-source Actions minutes:

```sh
python3 tools/build-docs-site.py --output artifacts/docs-NEW
python3 tools/package-docs-site.py --site-dir artifacts/docs-NEW/site \
  --build-report artifacts/docs-NEW/build-report.json --output artifacts/docs-publication-NEW
python3 tools/manage-github.py docs --input artifacts/docs-publication-NEW
python3 tools/manage-github.py docs --input artifacts/docs-publication-NEW --apply
```

Only the checked docs-only bundle is uploaded. Verify publication and the actual
site separately; retry concurrent writes from fresh state without force-pushing.

## Exact-commit local validation

```sh
python3 tools/validate-local.py --base origin/main --output artifacts/local-validation-NEW
```

Run on a clean committed branch. The receipt records source/base commits, changed
paths, platform, interpreter, actual commands/exit codes and log hashes. Failed
checks or changed source cannot pass. Rerun after any new commit.

The maintenance profile covers Python management tools/tests and maintained docs:
all Python fixtures and strict docs/link checks must pass. For maintenance PRs
whose hosted jobs are refused before starting, this exact-head receipt can
replace unavailable hosted checks. Post commit, profile, commands, results, log
hashes and the hosted failure reason in the PR. Inspect the diff and acceptance
criteria; never fabricate check success or claim Windows/multiple-Python passes.

Other changes run all local C/ASan/UBSan checks, native RPX/WUHB and docs builds.
Runtime, GUI, packaging, dependency and workflow changes still need their required
platform evidence. Workflow edits need pinned actionlint/hosted review and fall
outside this scoped exception. Continue those features in visible draft PRs
while validation is collected locally or on an available runner.

Local tests and cross-builds do not accept physical Wii U gates. After merge,
delete/verify both topic branches and update every surface listed in
[AGENTS.md](../AGENTS.md#update-after-each-meaningful-change).
