# CTR2WUP

[![CI](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml/badge.svg)](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml)

Experimental 3DS-to-Wii U static recompilation. The goal is a Windows injector
that creates an individual WUP title, with the top screen on the TV and the
bottom screen, touch and controls on the GamePad.

**Commercial games, playable-game conversion and the Windows GUI are not
available yet.** Current builds provide developer tools and original homebrew
diagnostics. Super Mario 3D Land is the first planned compatibility target.

[Documentation](https://jdiller-86.github.io/CTR-2-WUP/) ·
[Downloads](https://github.com/Jdiller-86/CTR-2-WUP/releases) ·
[Development plan](plans.md) ·
[Issues](https://github.com/Jdiller-86/CTR-2-WUP/issues) ·
[Contributing](CONTRIBUTING.md)

## What works today

- Bounded container inspection, optional local decryption and verified asset preparation.
- ARM/Thumb/VFP translation foundations and checked guest memory/services.
- Original compiled guests with dual screens, input, SD saves and assets.
- GSP/PICA processing, bounded software scenes and CSTM/ADPCM stream mixing.
- Reproducible RPX/WUHB diagnostics and independently verified original WUP candidates.

See [current limits](docs/compatibility.md) before interpreting a build as game
support. The native runtime has reviewed Wii U evidence; installed-title and
new runtime paths have separate acceptance requirements.

## Use or build

For original diagnostic downloads, follow [getting started](docs/getting-started.md)
and the single guide inside the chosen package. SD files can be copied by FTP.
Test instructions and return-folder details ship with each package.

Host development needs Python 3.12+, GNU Make and a C compiler:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -e '.[packaging,decryption]'
make help
make check
```

[Contributing](CONTRIBUTING.md) covers native SDK setup and validation;
[Windows builds](docs/build-windows.md) covers the Windows toolchain.

## Source map

| Path | Purpose |
| --- | --- |
| `host/` | PC inspection, preparation, builds and packaging |
| `translator/` | AOT integration and generated-code boundary |
| `renderer/` | Host graphics preparation |
| `runtime/core/` | Portable guest CPU, services, graphics and audio |
| `runtime/wiiu/` | Native Wii U platform and adapters |
| `tests/` | Original fixtures, references and reviewed hardware evidence |
| `tools/` | Build, validation and package generators |
| `docs/` | Usage and technical contracts |
| [plans.md](plans.md) | Next development steps and milestone links |

Generated output belongs in ignored `artifacts/`; ROMs, keys and derived game
data stay in ignored `private/`. Never attach private game data to public issues.

## License

[MIT](LICENSE). Dependencies retain their own licenses; see
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Builds use
[devkitPro](https://devkitpro.org), [WUT](https://github.com/devkitPro/wut) and
[libctru](https://github.com/devkitPro/libctru). The planned injector workflow
is inspired by [UWUVCI AIO](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF).
