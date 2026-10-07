# Packaging investigation and validator

No installable WUP has been generated. The RPX and WUHB are original homebrew
diagnostics. WUHB menu shortcuts and RPX launch are different from WUP install.

The two requested packer sources have no identified license grant. Both create
unauthenticated signatures and require an appropriate patched/homebrew console
environment; both also echo key values in upstream logs. They were researched,
not incorporated or run. See `foundation-audit.md` for exact pins/build errors.

Original `host.package_validator` implements a narrow RSA2048/SHA256 encrypted
Wii U application package structural profile:

- Bounds, title/TMD/ticket agreement, boot index and unique content indices/IDs.
- SHA256 content-info table/group verification and declared content sizes.
- H3 SHA1 verification for the supported hashed-content profile.
- Missing, extra, malformed and symlink content rejection.
- Explicit local title registry ownership and atomic registry writes.

Run `python -m host validate-package PATH`. Results explicitly state
`structure_only`, unverified plaintext integrity (requires a local key),
unverified signatures/certificate chain and unrun console installation. A
positive structure result is never proof of an authentic or installable title.
The synthetic test fixture is deliberately not an installable package and is
generated only inside temporary test directories.

An explicit reservation is available with:

```sh
python packaging/validate.py PATH --registry private/title-registry.json --reserve "my project identity"
```

The registry covers only this user's local project allocations. It does not
prove absence of collisions on a console or in other projects. Future title
allocation must check a user-supplied console inventory and require an explicit
identity override. System/title types outside `00050000` and zero identities are
rejected. No claimed globally safe homebrew range is invented. Same-owner
rebuild reservations may update the package revision while keeping the title
identity stable; another owner cannot take it. Serialize concurrent registry
writes. A package TMD digest is a revision check, not the game's save identity.

Next required work: license-cleared packing and certificate provenance, local
key handling with redacted diagnostics, original title metadata/artwork and
staging, plaintext/decryption integrity validation, installer/signature policy
validation, and physical install/menu/rebuild/save/uninstall checks. Never
automate installation or replacing console titles without user direction.
