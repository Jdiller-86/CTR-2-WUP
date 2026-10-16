# Compiler-built original homebrew

The optional developer workflow builds original C applications with the
normal devkitARM 3DSX CRT/newlib and clean unmodified libctru source pinned at
`9b55eda44cf80b971503991e0c78f9bc8fe50425`. It does not replace `__appInit`,
library startup, locks, threads or input functions with test stubs. The trusted
original ELF segments are placed in an original synthetic NCCH for the existing
checked loader; this is not a general ELF or 3DSX importer.

```sh
bash tools/install-arm-fixtures.sh
python3 tools/validate-libctru.py --output artifacts/reports/libctru-local
python3 tools/validate-libctru.py --application interactive --wiiu \
  --output artifacts/reports/libctru-console
python3 tools/validate.py --compiled-homebrew
```

The source path defaults to `../ctr2wup-deps/libctru`; use `--libctru-source`
and `--devkitarm` elsewhere. The tool verifies a clean pin, builds a temporary
library; `install-arm-fixtures.sh` extracts devkitARM from the same immutable
official SDK image, verifies its compiler hash and checks out the clean library
pin without replacing existing dependencies. `CTR2WUP_SDK_ROOT` and
`CTR2WUP_LIBCTRU_SOURCE` can select other setup paths. The validator builds a temporary
library, and preserves compiler logs, hashes, generated AOT and guest results
under the requested new output directory. It also supports `--sanitize`,
`--cc-prefix`, `--run-prefix` and emulated PPC Linux `--static`. Dependencies stay
external. Console compilation stages generated sources under controlled relative
paths so chosen output and checkout paths can contain spaces/Unicode; RPX/WUHB
are checked before copying them to the requested location. See [Windows builds](build-windows.md) for native host/toolchain requirements.

The applications test:

- `boot`: normal CRT/library startup, reaching main and orderly process exit.
- `threaded`: real `threadCreate/threadJoin/threadFree`, LightLock, a 1,024-count
  shared result and initialized C `__thread` data isolated between contexts.
- `graphics`: real libctru graphics/event thread, CPU-linear double buffers,
  checked cache flush, shared GSP queue and VBlank waits. Three full RGB565 frames
  must decode to green/red/green on top and blue below; six PPMs are preserved.
- `input`: real `hidScanInput`, held/down/up edges, signed Circle Pad and touch
  coordinates, including release, through three standard shared-ring updates.
- `filesystem`: normal savedata mount and newlib file I/O, Japanese UTF-16
  path, delete, explicit commit, fresh-process reload and independently checked
  snapshot bytes. Corrupt snapshots are rejected and preserved. Its optional
  RPX/WUHB writes only an original private diagnostic save.
- `interactive`: standard input drives three exact top-screen colors, bottom
  touch pixels at (17,23)/(319,239), release and the guest's own B-button exit.
  Its optional RPX/WUHB uses live VPAD input and the same checked AOT/GSP runtime.

- `romfs`: ordinary root/nested/Unicode file reads, seek/EOF, directory listing,
  write denial and unmount through unmodified libctru's Level-3 parser. Its
  current-process view contains only original assets; see `romfs-assets.md`.

- `transfer`: standard GX DMA, fill, tiled display transfer, raw texture copy
  and cache commands with checked pixels, padding and event waits; see
  [GX transfers](graphics-transport.md).

- `draw`: standard GX/PICA lists, owned tiled targets, retained shader/state,
  exact target/transfer/display assertions and own PASS/exit. See
  [compiled guest draws](guest-pica-draws.md) for the bounded fragment contract.

The validator runs all nine applications by default. Each must produce its own
PASS, ExitProcess and zero fallback. Pixel checks inspect every pixel; input
uses the public HID producer rather than writing a private app result.

Standalone APT and an empty read-only virtual SDMC support ordinary CRT/APT/FS
startup. The boot app checks active state and `aptMainLoop`.
`--application boot --trace-startup` records requests. Known unavailable lookups
include `APT:S`, `APT:A` and optional `ptm:sysm`; privileged/system services and
general applets are not supplied. [Filesystem](filesystem.md) defines persistence
and ownership. Scoped graphics profiles do not establish general PICA behavior,
retail saves, DSP audio, CRO loading or commercial compatibility.
