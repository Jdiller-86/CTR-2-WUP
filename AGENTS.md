# Continuing CTR2WUP

Read STATUS.md and docs/milestones.md before making compatibility claims.
The user-supplied requirements are in docs/project-brief.md. Build source in
the existing isolated checkout; do not create a worktree unless asked.

- Run `make check` for Python plus C tests/ASan/UBSan; `make prototype` for RPX/WUHB.
- Toolchain setup: `bash tools/install-wiiu-sdk.sh`; optional host lifter:
  `bash tools/install-translator.sh`. Preserve signature/checksum/TLS verification.
- Keep ROMs, assets, translated commercial code, saves, packages, traces and
  credentials under ignored private directories. Never include them in source
  bundles, logs or public artifacts. Synthetic fixtures must be original.
- Never report console execution from compilation or desktop passes. Unsupported
  CPU/services/GPU states must fail visibly. Structural package validation does
  not establish authenticity or installer acceptance.
- Maintain STATUS.md, milestone evidence, architecture/build/compatibility docs
  and versioned reports. Do not add a GUI before the execution pipeline works.
- For Codex-generated commits, install the opt-in repository hook with
  `python3 tools/codex-attribution.py install`, then use `git codex-commit`.
  Preserve existing hooks and human attribution. See CONTRIBUTING.md for identity
  and authentication limits; never rewrite history solely to add attribution.
