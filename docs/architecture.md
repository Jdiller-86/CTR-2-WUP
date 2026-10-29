# Architecture

CTR2WUP is developing a Windows application that turns one supported, owned 3DS
revision into a standalone Wii U title. The engine must demonstrate execution,
rendering, audio, saves and real package installation before the GUI presents a
conversion as supported. Current deliverables are original diagnostics and host tools.

## Source map

| Directory | Responsibility |
|---|---|
| `host/` | Python CLI, input inspection/preparation, loading, build orchestration, staging and WUP verification |
| `translator/` | Original generated-C backend and pinned external AOT integration |
| `runtime/core/` | Portable checked CPU, memory, scheduler, services, software graphics, Latte encoder and audio |
| `runtime/wiiu/` | WUT platform, GX2 scene/presentation adapters, VPAD, AX, ProcUI and diagnostic logging |
| `renderer/` | Host-side command, texture and framebuffer reference tooling |
| `packaging/` | Original title profiles and packaging policy |
| `tests/` | Original fixtures, regression/reference checks and reviewed hardware baseline |
| `tools/` | Build, validation and package generation commands |
| `docs/` | Stable usage and technical contracts |

## Pipeline

1. Inspect the container and identify the exact executable revision.
2. Prepare encrypted input locally when required; validate recovered code and assets.
3. Load checked executable segments and analyze/lift supported code.
4. Compile generated C against the shared runtime and Wii U backend.
5. Stage original metadata, artwork, executable and owned assets.
6. Construct and independently verify the WUP.

Each stage has explicit inputs, reports and unsupported failures. The CLI provides
the reusable engine. Inspection, translation, package integrity and physical
compatibility are separate facts. No stage treats a generic-success service stub
or a valid package structure as proof that a game works.

## Portable runtime

Guest addresses remain 32-bit integers. Checked little-endian mappings enforce
permissions, ranges, alignment and ownership on every host. Writes through
executable aliases invalidate AOT snapshots and the exclusive monitor.
[CPU execution](cpu.md) shares one process and deterministically scheduled guest
threads. [Memory](guest-memory.md) and [services](services.md) retain references
independently of handles. Unsupported interfaces fail with PC/opcode or IPC context.

[Display and transfer](graphics-transport.md) code reads CPU-backed guest images.
[Software scenes](software-scenes.md) provide a bounded reference for
[guest draws](guest-pica-draws.md). The optional native scene backend translates
PICA MOV/ADD/MUL/NOP/END to Latte vertex code and submits original inputs through
GX2. [Base textures](textures.md) support nearest/bilinear Texture0/1/2 sampling,
independent coordinates and bounded
six-stage RGB/alpha TEV with a delayed combiner buffer, alpha tests, logic/blend operations, write masks and
bounded D16/D24S8 depth/stencil; decoded
uploads retain guest UV and explicit ownership.
Versioned immutable source snapshots and exact-content decoded-image caches
reuse CPU resources while checking current permissions, aliases and bytes.
Complete software preflight and byte-exact GPU readback precede atomic guest
commit; unsupported state or backend failure cannot fall back silently.
Physical GPU parity remains unverified. [Audio](audio.md) separates checked
decoding/mixing, bounded original guest DSP PCM and owned finite AX buffers.
General firmware profiles and continuous guest/AX synchronization remain open.

[Filesystem](filesystem.md) operations use platform-owned private volumes, not
host paths derived from guest names. Save publication uses checked pending/backup
states. [RomFS](romfs-assets.md) exposes an immutable bounded provider.

## Wii U ownership and diagnostics

The WUT backend owns TV/GamePad surfaces, VPAD/touch, AX voices, ProcUI and reports.
Foreground loss suspends guest input/rendering. Cleanup releases guest, audio,
graphics and report resources before system transition. A fixed-size result TXT
is independent of the full trace; identity, write, close and read-back failures
are sticky. Guest state is captured before teardown, and final results precede exit.
A saved result does not itself establish successful Menu return or SD durability.

## Windows product contract

The planned C# WPF application uses a versioned engine interface. The default flow
is **choose game → configure title/artwork/output → Inject → finished WUP**.
It automatically selects a supported exact-revision profile, reports actual stages,
remains responsive, supports cancellation/retry and exposes detailed diagnostics
only when needed. Routine conversion must require no compiler commands or manual
file editing. Stable collision-checked per-game identities preserve saves on rebuild.
Top screen defaults to TV; bottom screen and touch default to GamePad.

Local decryption uses the user's own selected key/seed exports with redacted
errors. Keys never enter generated code, exported projects, reports or WUPs.
The reference interaction is
[UWUVCI AIO](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF);
no code, artwork, commercial donor or binaries were imported from it.
