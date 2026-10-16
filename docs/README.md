# Documentation

CTR2WUP is developing a Windows injector that converts a supported, owned 3DS
game into a per-game Wii U WUP. Current builds provide original diagnostics and
developer tools. Commercial game conversion and the injector GUI are unfinished.

## Use the tools

- [Getting started](getting-started.md): downloads, SD layout and diagnostic launch.
- [Compatibility](compatibility.md): supported inputs and execution boundaries.
- [Encrypted inputs](encrypted-input.md) and [CIA intake](cia-intake.md): local preparation.
- [Private asset samples](private-asset-samples.md): bounded analysis exports.
- [Packaging](packaging.md): staging, WUP construction and verification.
- [Troubleshooting](troubleshooting.md): build, storage and launch problems.

Each diagnostic package includes its own short instructions. Follow those
instructions for that package; the documentation describes stable usage and
technical interfaces.

## Develop

Start with [Contributing](../CONTRIBUTING.md), [Windows builds](build-windows.md)
and [Testing](testing.md). [Architecture](architecture.md) maps the source and
execution pipeline. The root [plans.md](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/plans.md)
contains current development priorities; GitHub issues hold work discussions.

The runtime references describe [CPU execution](cpu.md), [memory](guest-memory.md),
[services and input](services.md), [display and transfers](graphics-transport.md),
[software scenes](software-scenes.md), [guest draws](guest-pica-draws.md),
[filesystem](filesystem.md) and [audio](audio.md). These are bounded contracts,
not claims of general 3DS compatibility.

Keep game dumps, keys, extracted assets, saves and commercial traces private.
