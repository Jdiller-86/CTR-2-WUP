# Contributing to CTR2WUP

CTR2WUP targets a Windows GUI that converts an owned 3DS game into a playable
Wii U WUP. Current builds are development tools and original diagnostics; the
commercial execution pipeline and GUI are still being implemented. Read
[STATUS.md](STATUS.md), the [milestones](docs/milestones.md) and
[AGENTS.md](AGENTS.md) before proposing compatibility or acceptance changes.

## Choose verifiable work

Check existing issues and the milestone checklist. For a bug, provide a minimal
reproduction with original data. For a feature, describe the resulting behavior,
its milestone and acceptance evidence. The issue forms also accept Wii U test
results, including missing reports, failed storage checks and hangs.

Use [development tracking](docs/development-tracking.md) for native milestone →
task → scoped feature → PR relationships. Assign the repository milestone and
Project membership, give the feature a native parent, and record its PR in
`docs/features.json`. Broad unfinished tasks remain open when a feature merges.

Do not upload ROMs, key databases, personal keys, extracted game assets, saves,
translated commercial code, game packages or credentials. Keep them under
ignored `private/` directories locally. Original diagnostic TXT files are useful
after reviewing them for private paths or data. Use [SECURITY.md](SECURITY.md)
for vulnerability reports instead of a normal issue.

## Build and check

Use Python 3.12+, GNU Make 4.2+ and a C compiler. Host checks do not need the Wii U SDK;
start with `make help` and `make check`. Install the verified Wii U SDK with the
repository script; the first installation requires Docker. Optional encrypted
input/packaging tests use the `cryptography` dependency. The target SDK uses
devkitPro's devkitPPC, WUT and Wii U tools; the host compiler runs desktop tests.

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -e '.[packaging,decryption]'
export CTR2WUP_SDK_ROOT="$PWD/../ctr2wup-sdk"
bash tools/install-wiiu-sdk.sh
export DEVKITPRO="$CTR2WUP_SDK_ROOT"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
make check
make prototype
```

`make check` runs Python and portable C suites with sanitizer checks.
`make prototype` builds RPX/WUHB diagnostics. Neither command proves physical
Wii U behavior or WUP installation. An existing official devkitPro installation
can be used by setting `DEVKITPRO` and `DEVKITPPC` to its SDK paths. For Windows,
use devkitPro's MSYS2 environment and signed `wiiu-dev` packages; the extracted
Linux SDK cannot run natively on Windows. Consult [Windows build notes](docs/build-windows.md),
[upstream integration](docs/upstream-integration.md) and the focused test suites
for requirements specific to your change. SDK installation must preserve
checksum, signature and TLS verification.

When hosted Actions refuses jobs before starting, the owner-authorized
[direct development route](docs/direct-development.md) supplies secure API
management and an exact-commit validation receipt. Only Python maintenance and
documentation changes have the scoped local merge exception; required platform
checks and physical acceptance remain distinct. Record actual PR validation
and delete both completed topic branches after merging.

## Console evidence and diagnostic releases

Use the versioned original diagnostic build and instructions for the requested
test round. Record its release tag or commit, displayed application version and
artifact checksum where available. A newer source version does not establish
that an older console result tested the new behavior.

For Aroma, copy a diagnostic kit's `sd-card/` contents to the SD root; WUHB apps
belong under `sd:/wiiu/apps/`. Matching RPX files require an RPX-capable launcher.
These files are homebrew applications, not WUP installation directories. Follow
[guided testing](docs/guided-testing.md) for controls and the expected
`sd:/ctr2wup/` TXT results. Preserve failed-run reports and explicitly report
when no new file appeared. Include exact firmware, Aroma and launcher versions
when known, plus observed exit/Menu behavior.

Published diagnostic releases and CI build artifacts demonstrate only their
documented original fixture/build checks. Commercial games, broader PICA rendering,
guest audio and installed playable WUP output still have separate acceptance
gates in the milestone checklist.

## Submit a pull request

Keep the change focused and explain the concrete before/after behavior. Link its
issue or milestone, record relevant checks and identify unavailable prerequisites.
Use the pull request template. Update affected status, architecture, compatibility
and build documentation when behavior or evidence changes; do not claim console
execution from compilation. Record evidence in versioned reports where the
existing workflow calls for them.

Develop on a separate topic branch and push a draft PR early. Link its scoped
feature using `Closes #...`, then keep the PR and issue visible in the same
milestone and Project. Merge only after required checks pass. Delete and verify
the completed remote and local topic branches after every merge.

New fixtures must be original and reproducible from source. Guest memory is
little-endian even on big-endian PowerPC; avoid native guest-word loads and keep
unsupported states explicit. Review licenses and notices before adding a
dependency or redistributing an executable. Generated builds belong under
ignored `artifacts/`; inspect release contents for private data before publication.

## Agent-assisted work

[Repository Copilot instructions](.github/copilot-instructions.md) and specialist
profiles under [.github/agents](.github/agents) provide CPU/runtime, host-pipeline,
graphics and console-evidence context. Supported clients can select them when
Copilot access is available. These files do not turn on paid services or autonomous
jobs. Agent changes receive the same review and validation as any contribution;
their summaries must identify pending physical and commercial acceptance checks.

## Codex commit attribution

For work generated by Codex, install the opt-in hook in this checkout:

```sh
python3 tools/codex-attribution.py install
git codex-commit -m "Describe the concrete change"
```

The wrapper records **Codex** as author using the verified OpenAI-owned
[ChatGPT Codex Connector bot](https://github.com/apps/chatgpt-codex-connector)
identity. The hook adds OpenAI's published trailer exactly once:
`Co-authored-by: Codex <noreply@openai.com>`.
The [published Codex attribution source](https://github.com/openai/codex/blob/c308e127e0e8cd040f670ca0e0496ae1c7519d3f/codex-rs/ext/git-attribution/src/world_state.rs)
records the commit and PR formats used here.
Add `Generated with [Codex](https://openai.com/codex/).` to the associated PR body.

Ordinary `git commit` retains your identity and message. Installation is local
to this repository, preserves global configuration and refuses conflicting hooks
or aliases. Use `python` if that is your Windows Python command. The public bot
email identifies commit authorship; it supplies no bot login, signature or access.
Pushes, PR creation and merges still use the connected account. Existing history
is preserved; GitHub decides how bot authors appear in contributor statistics.
