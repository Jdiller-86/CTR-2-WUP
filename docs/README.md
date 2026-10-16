# Documentation

CTR2WUP is an experimental 3DS-to-Wii U static recompilation project. Current
builds provide original diagnostics and developer tools; commercial game
conversion and the Windows injector GUI are unavailable.

## Use the tools

- [Getting started](getting-started.md): downloads, SD layout and diagnostic launch.
- [Compatibility](compatibility.md): supported inputs and execution boundaries.
- [Encrypted inputs](encrypted-input.md) and [CIA intake](cia-intake.md): local preparation.
- [Private asset samples](private-asset-samples.md): bounded analysis exports.
- [Packaging](packaging.md): staging, WUP construction and verification.
- [Troubleshooting](troubleshooting.md): build, storage and launch problems.

Diagnostic packages include deployment instructions and result-folder details.

## Develop

Start with [Contributing](../CONTRIBUTING.md), [Windows builds](build-windows.md)
and [Testing](testing.md). [Architecture](architecture.md) maps the source and
execution pipeline.

[Project graph](https://jdiller-86.github.io/CTR-2-WUP/progress/) ·
[Issues](https://github.com/Jdiller-86/CTR-2-WUP/issues) ·
[Development plan](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/plans.md)

The runtime references describe [CPU execution](cpu.md), [memory](guest-memory.md),
[services and input](services.md), [display and transfers](graphics-transport.md),
[software scenes](software-scenes.md), [guest draws](guest-pica-draws.md),
[filesystem](filesystem.md) and [audio](audio.md). These are bounded contracts,
not claims of general 3DS compatibility.

Keep game dumps, keys, extracted assets, saves and commercial traces private.
