# Working on CTR2WUP

Read [plans.md](plans.md) for the next work and [README.md](README.md) for the
source map. Read the affected technical contract in `docs/` before changing it.
Before creating or changing a Wii U app, read and follow
[the platform protocol](docs/wiiu-platform.md); reuse the linked native helpers
for storage, AX, GX2 and ProcUI rather than reimplementing those fixes.

- Build the shared execution/conversion engine toward a playable per-game WUP;
  the Windows GUI follows a working pipeline. Original diagnostics prove only
  their exercised behavior.
- Use focused branches and PRs. Run relevant tests; runtime changes need
  `make check` and native code/build changes also need `make prototype`.
  Documentation changes need the strict docs build. Keep sanitizer, signature,
  checksum and TLS checks enabled.
- Guest ARM memory is little-endian; Wii U PowerPC is big-endian. Use checked
  byte-oriented access and fail explicitly for unsupported states.
- Keep ROMs, keys, commercial assets/code/saves/traces and credentials in ignored
  `private/`, never in commits, logs, issues or public artifacts. Fixtures must be
  original. Preserve dependency pins, licenses and notices.
- Reuse reviewed hardware evidence within its scope. A version bump is no reason
  to retest. Request hardware work only for a specific changed path or unresolved
  criterion; explain why existing evidence cannot answer it. Put one short guide
  in the generated package and accept one FTP return folder.
- Keep README and documentation concise, factual and professional: purpose,
  usage and technical contracts. Exclude prompts, personal work history and
  progress discussion. User-facing instructions fit a few short paragraphs.
- Keep detailed scope, acceptance criteria, dependencies, evidence and progress
  in native issues and the Project. `plans.md` identifies the next implementation
  priorities; the dynamic graph reads live tracking data. Do not duplicate it in
  editable status catalogs or per-version documentation.
- Use `git codex-commit` for Codex attribution. Merge tested changes, verify
  publication when affected, and remove completed topic branches.
