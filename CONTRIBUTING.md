# Contributing

Read [plans.md](plans.md) and the affected technical contract in `docs/`.
Use an issue for a concrete bug or delivery; include the problem, desired
behavior and relevant logs. Keep ROMs, keys, commercial assets/code/saves/traces
and credentials out of source, issues and public artifacts. Use original fixtures.
Vulnerability reports follow [SECURITY.md](SECURITY.md).

## Build and validate

Host checks need Python 3.12+, GNU Make 4.2+ and a C compiler:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -e '.[packaging,decryption]'
make check
```

For native RPX/WUHB builds, install the verified SDK (first setup needs Docker),
or use an existing official devkitPro installation:

```sh
export CTR2WUP_SDK_ROOT="$PWD/../ctr2wup-sdk"
bash tools/install-wiiu-sdk.sh
export DEVKITPRO="$CTR2WUP_SDK_ROOT"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
make prototype
```

See [Windows builds](docs/build-windows.md) for native Windows setup.
Run tests relevant to the change. Runtime changes need `make check`, including
ASan/UBSan; native code/build changes also need `make prototype`. For docs, run
`python tools/build-docs-site.py` with `docs-requirements.txt` installed.
Keep dependency signatures, checksums, TLS verification and sanitizers enabled.

## Pull requests

Use a focused branch. Explain the problem, resulting behavior and actual checks;
link an existing issue when useful. Update only affected contracts, usage or
[plans.md](plans.md). Merge after required hosted checks pass and delete the
completed topic branch.

Desktop tests, cross-builds and physical Wii U results prove different things.
Record hardware validation in the relevant issue, including source identity,
environment, observations and diagnostic logs. Packages include deployment
instructions and a result folder suitable for FTP transfer.

Guest ARM memory is little-endian; Wii U PowerPC is big-endian. Use checked access,
keep unsupported states explicit and preserve dependency licenses/notices.

For Codex-generated changes, install the repository attribution hook with
`python3 tools/codex-attribution.py install`, commit using `git codex-commit`,
and include `Generated with [Codex](https://openai.com/codex/).` in the PR body.
