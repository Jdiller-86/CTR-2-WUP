# Working on CTR2WUP

Read [plans.md](plans.md) for the next work and [README.md](README.md) for the
source map. Read the affected technical contract in `docs/` before changing it.

- Build the shared execution/conversion engine toward a playable per-game WUP;
  the Windows GUI follows a working pipeline. Original diagnostics prove only
  their exercised behavior.
- Use focused branches and PRs. Run relevant tests; implementation changes need
  `make check` and, when native code/build inputs change, `make prototype`.
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
- Update only documentation affected by behavior. `plans.md` holds the current
  implementation sequence; native GitHub issues/milestones hold work history.
  Do not add status mirrors, feature catalogs, per-version test pages or process
  reports. Git history retains removed material.
- Use `git codex-commit` for Codex attribution. Merge tested changes, verify
  publication when affected, and remove completed topic branches.
