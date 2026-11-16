# Title profiles

No verified playable commercial profile is enabled. The experimental
`mario3dland-usa-test-v1` intake profile accepts USA base applications with RomFS;
each package binds its exact executable and asset hashes and records unverified
gameplay. See [the first-title contract](../docs/first-title.md).

Supported profiles must additionally declare runtime/translator versions, exact
input/executable/module hashes, guest platform, services/GPU states, title/save
identity policy and actual console evidence. User-derived code, shaders, ROMs,
saves and traces stay outside source control.
