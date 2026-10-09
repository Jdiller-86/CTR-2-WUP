# Compiler-built homebrew evidence

The optional v0.13 developer workflow builds original C applications with the
normal devkitARM 3DSX CRT/newlib and clean unmodified libctru source pinned at
`9b55eda44cf80b971503991e0c78f9bc8fe50425`. It does not replace `__appInit`,
library startup, locks, threads or input functions with test stubs. The trusted
original ELF segments are placed in an original synthetic NCCH for the existing
checked loader; this is not a general ELF or 3DSX importer.

```sh
source /workspace/ctr2wup-tools/environment/activate.sh
bash tools/install-arm-fixtures.sh
python3 tools/validate-libctru.py --output artifacts/reports/libctru-local
python3 tools/validate-libctru.py --application interactive --wiiu \
  --output artifacts/reports/libctru-console
python3 tools/validate.py --compiled-homebrew
```

The source path defaults to `../ctr2wup-deps/libctru`; use `--libctru-source`
and `--devkitarm` elsewhere. The tool verifies a clean pin, builds a temporary
library. `install-arm-fixtures.sh` extracts devkitARM from the same immutable
official SDK image, verifies its compiler hash and checks out the clean library
pin without replacing existing dependencies. `CTR2WUP_SDK_ROOT` and
`CTR2WUP_LIBCTRU_SOURCE` can select other setup paths. The validator builds a temporary
library, and preserves compiler logs, hashes, generated AOT and guest results
under the requested new output directory. It also supports `--sanitize`,
`--cc-prefix`, `--run-prefix` and emulated PPC Linux `--static`. Dependencies stay
external; a future native Windows toolchain setup needs its own validation.

The applications test:

- `boot`: normal CRT/library startup, reaching main and orderly process exit.
- `threaded`: real `threadCreate/threadJoin/threadFree`, LightLock, a 1,024-count
  shared result and initialized C `__thread` data isolated between contexts.
- `graphics`: real libctru graphics/event thread, CPU-linear double buffers,
  checked cache flush, shared GSP queue and VBlank waits. Three full RGB565 frames
  must decode to green/red/green on top and blue below; six PPMs are preserved.
- `input`: real `hidScanInput`, held/down/up edges, signed Circle Pad and touch
  coordinates, including release, through three standard shared-ring updates.
- `interactive`: standard input drives three exact top-screen colors, bottom
  touch pixels at (17,23)/(319,239), release and the guest's own B-button exit.
  Its optional RPX/WUHB uses live VPAD input and the same checked AOT/GSP runtime.

All require a real guest PASS, ExitProcess and zero interpreter fallback. Pixel
checks inspect every pixel, not only a debug message. Input tests use the public
HID producer; no host writes a private application result or skips guest code.
The interactive program includes a 1 ms sleep per frame to expose boundaries
and avoid polling between presentation and the next input sample.

APT/FS startup remains unavailable: each application records eight failed IPC
requests tolerated by this pinned libctru. A single-instruction boot trace shows
six failed APT:S/A/U lookups, one fs:USER lookup and one invalid-session request
after FS acquisition failed. Use `--application boot --trace-startup` to capture
all 11 requests; `--sanitize` is also supported. Reaching main does not prove
those services initialized. No SD/RomFS,
saves, DSP/audio, CRO loader, PICA commands, GPU physical addresses or commercial
compatibility is claimed. The interactive build's native tone is a platform
diagnostic, not guest DSP audio. There is no Wii U hardware or Windows execution
result. Do not enable a commercial profile from these homebrew passes.

The original source is under `tests/homebrew`. Optional locally generated guest
images link external libctru/newlib/compiler support. The library's full license
is preserved at `licenses/libctru.txt`; retain this and the existing runtime
notices when distributing the compiled interactive artifact. The MPL 2.0 3DSX
CRT's corresponding unmodified assembly/linker/spec sources, full license and
section/relocation audit are supplied under `licenses/devkitarm-crtls/`. No ROM, game assets,
keys, certificates or proprietary SDK is part of this workflow.
