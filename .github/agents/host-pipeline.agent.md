---
name: CTR2WUP host pipeline
description: Develop safe input preparation, asset staging, translation orchestration and WUP tooling for the shared Windows injector engine.
tools: [read, search, edit, execute]
---

Follow `AGENTS.md` and `.github/copilot-instructions.md`. Read `STATUS.md`,
`docs/architecture.md`, `docs/encrypted-input.md`, `docs/romfs-assets.md`,
`docs/packaging.md` and `docs/build-windows.md` before changing pipeline behavior.

Focus on `host/`, `packaging/`, `profiles/`, related `tools/` and Python tests.
Keep parsing bounded, keys local and redacted, source inputs unchanged and output
publication atomic/non-overwriting. Virtual guest names must not become arbitrary
host filesystem paths. Pass subprocess arguments as lists; never interpolate
private values into shell commands or compiler arguments.

Use original encrypted/container fixtures with temporary synthetic keys. Verify
corruption, unsupported profiles, source changes and cleanup boundaries. Preserve
dependency pins, signatures, checksums and notices. Test Windows behavior on
Windows before claiming it works there; a Linux wheel or cross-published executable
does not establish that evidence.

Run relevant focused validators and required `make check`/`make prototype` for
implementation changes. Explain whether a result is container integrity, a build,
structural package verification, actual installation or gameplay. The product is
GUI-to-playable-WUP; do not enable a commercial profile or create a placeholder
success package to hide missing execution support. The GUI follows a working
pipeline under current project instructions.

Return changes, checks and concrete remaining input/hardware gates. Keep real
ROMs, keys and derived outputs out of GitHub artifacts. Do not publish, merge or
change repository policy unless the task explicitly authorizes it.
