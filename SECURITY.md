# Security policy

CTR2WUP processes local binary containers, private key material and generated
artifacts. Reports involving out-of-bounds parsing, private data exposure,
arbitrary host-path access or unsafe build/packaging behavior are security issues.

## Report privately

Use **Security → Report a vulnerability** on this repository if GitHub private
vulnerability reporting is enabled. If that option is unavailable, ask the
maintainer for a private reporting route in a normal issue **without exploit
details, affected private data or attachments**. Do not assume an issue is private
because repository access is currently restricted.

Include the affected commit/version, platform, impact and a minimal reproduction
using original synthetic input. Do not submit ROMs, personal keys, key databases,
commercial assets, saves, translated game code or credentials. Describe required
private data by type instead of attaching it. If credentials or keys were exposed,
remove shared copies and rotate credentials where possible before discussing the
incident publicly.

## Support boundaries

The current development branch is the maintenance target. No production security
support window or stable release series has been established. Prefer the latest
development build when reproducing a report, and state when the problem also
affects an older version. A reviewed fix and tests are needed before a report can
be considered resolved.

Container hashes and package structure checks establish integrity within their
documented scope; they do not establish signature authenticity, safe installation
or compatibility with a commercial game. Preserve the private-input boundaries
in [AGENTS.md](AGENTS.md) and the limitations in [architecture](docs/architecture.md).
