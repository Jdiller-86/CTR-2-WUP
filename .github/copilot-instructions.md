# CTR2WUP repository instructions

Read [AGENTS.md](../AGENTS.md), [STATUS.md](../STATUS.md),
[milestones](../docs/milestones.md) and the [architecture](../docs/architecture.md)
before changing behavior or compatibility claims. AGENTS.md is the shared project
instruction source; these instructions and specialist profiles supplement it.

The goal is an UWUVCI-style Windows GUI that translates an owned 3DS game and
creates a playable per-game Wii U WUP. The shared CLI/runtime is the pipeline
foundation. The GUI follows verified execution; do not substitute more diagnostic
screens for the product or claim commercial support from synthetic fixtures.

- Use original fixtures and checked byte-oriented memory access. Guest ARM data
  is little-endian; the Wii U target is big-endian PowerPC. Unsupported CPU,
  service, GPU and package states must fail visibly.
- Keep all real ROMs, keys, assets, saves, translated commercial code, packages,
  traces and credentials in ignored private directories. Do not copy them into
  commits, issue comments, Actions output, artifacts or release archives.
- Run `make check` and `make prototype` for implementation changes. Use the
  focused suites and reference validators relevant to changed behavior.
  Preserve SDK checksums, signature and TLS verification.
- Separate desktop execution, emulated PowerPC tests, cross-compilation,
  native Windows execution and physical Wii U evidence. Packaging integrity is
  separate from authenticity and successful installation.
- Update status, milestone evidence, affected architecture/compatibility/build
  docs and versioned reports. Read current evidence instead of hard-coding a
  progress percentage or assuming a milestone is accepted.
- Keep changes reviewable. Prefer fixes and tests that prove failure boundaries
  over tests that merely mirror implementation. Do not disable checks or invent
  passing evidence to unblock a task.

Specialist profiles under `.github/agents/` are available to supported Copilot
clients when the account and repository have access. Their presence does not
enable a subscription, configure credentials or schedule unattended execution.
