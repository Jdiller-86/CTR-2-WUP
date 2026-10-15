# GitHub management and development continuity

The source repository is public by the owner's October 14 decision. Hosted
Actions now runs, including Windows, GCC/Clang sanitizers, Wii U and documentation
checks. Prefer **Reviewed GitHub management**, **Development traceability** and
**Project tracking** on immutable `main`, using the existing Actions management
secret. The Actions credential has successfully read personal Project #2.

The direct environment binding remains a fallback whose actual Project access
must be verified. A configured/ready binding alone is not proof of permission.
No credential is retrieved from Actions or copied into source, logs or artifacts.
The earlier maintenance-only local merge exception remains available for a future
hosted outage; it does not waive runtime/platform evidence.

The owner removed branch rulesets on October 15 and merged tested PR #106.
Required local/hosted validation remains repository policy. The former GitHub
one-review rule no longer blocks tested changes; no administrator bypass is
needed. Pages verification retries old-source/missing-route/transient HTTP and
freshness failures up to six attempts, ten seconds apart. Privacy/schema errors
fail immediately, and receipts retain safe error codes/attempts without response
bodies. Run `python3 tools/verify-project-site.py --output artifacts/site-check.json`
for the fixed canonical endpoints. The first 0.25.0 post-merge publication is
[verified run 38745496120](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38745496120).

## Reviewed Actions operations

Dispatch **Reviewed GitHub management** from `main` with a fixed operation and
`apply=false`. Inspect its receipt, then dispatch the same operation with
`apply=true`. For the bounded Mario transition choose operation `review` and
review `mario-input-received-v023`. Traceability, evidence and progress are
separate operations. After canonical publication, `legacy-docs` plans/applies
reviewed redirects and `verify-sites` checks both sites over HTTP without writes.
`overview` refreshes only the managed Project description while preserving owner
notes and existing fields/views. `archive-legacy` retires the legacy repository
only after redirect verification; it restores write access if Pages no longer verifies.
The legacy repository is now archived with live redirects. Migration operations
remain for reviewed recovery; routine publication uses canonical source only.
All privileged runs resolve and check out immutable trusted
main; no arbitrary PR code runs with the secret. Public receipts contain managed
identities/counts/operations, never raw Project resource notes or credentials.

## Secure credential setup

In the cloud environment's secure secret settings, bind
`CTR2WUP_GITHUB_MANAGEMENT_TOKEN` for `api.github.com`. Use the same management
identity as Actions, independently supplied by the owner. If its token value is
unavailable, create a classic personal access token for **Jdiller-86** with
**repo** and **project** scopes. These cover the source repository, personal Project
#2 and the source Actions dispatch path. Actions secrets cannot be read back.
Never paste a token in chat, arguments, logs, source or receipts, or retrieve it
from Actions. The direct tool never silently selects another credential.

```sh
python3 tools/manage-github.py preflight
```

The full preflight checks fixed repository identities/write permission and all 42
managed Project items/fields. Each operation checks only the access it needs:
evidence/docs can proceed while Project access is missing. Project read access alone does
not prove mutation access: supply read/write Project scope. A ready binding or
successful issue write is insufficient. The environment binding previously returned
an integration access error while the Actions identity works. Retain the actual
ignored access receipt when diagnosing either route.

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
links. Evidence refresh changes managed descriptions only. The reviewed
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

Documentation and the aggregate dashboard share the public source repository
and canonical Pages deployment. Direct publication validates the checked bundle
and dispatches the fixed trusted-main workflow; it never writes a generated tree
into source:

```sh
python3 tools/build-docs-site.py --output artifacts/docs-NEW
python3 tools/package-docs-site.py --site-dir artifacts/docs-NEW/site \
  --build-report artifacts/docs-NEW/build-report.json --output artifacts/docs-publication-NEW
python3 tools/manage-github.py docs --input artifacts/docs-publication-NEW
python3 tools/manage-github.py docs --input artifacts/docs-publication-NEW --apply
```

The workflow stages only checked documentation and allowlisted aggregates into a
Pages artifact. Verify its deployment and actual HTTP content separately. There
are no cross-repository generated commits or force pushes.

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

## Independent membership readback

A successful add mutation is insufficient proof of Project membership. A small
missing set can be verified through the issue/PR's independent `projectItems`
edge with exact content and selected Project identities. Public receipts expose
only repository references/counts/booleans and retain the original connection
readback separately. Archived entries and ambiguous/invalid identities fail;
no Project archive/settings change is inferred from a missing connection row.
See [GitHub's PullRequest schema](https://docs.github.com/en/graphql/reference/pulls#pullrequest).

The independent read route is live-verified in
[apply 38752814566](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38752814566):
connection readback sees 109/111 members, while exact reverse links independently
confirm the two active additions and verify the complete 111-member set.
[Publication 38752341776](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38752341776)
verifies source 150aa5c with fresh available Project data. Earlier failed receipts
remain evidence; Project settings and owner notes were preserved.
