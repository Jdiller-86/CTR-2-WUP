# Foundation audit

Audited 2026-10-07 against source obtained through the existing authenticated Git transport. Every requested repository was reachable. Sources are outside this project at `/workspace/ctr2wup-deps`; none of the seven candidate source trees was vendored into CTR2WUP. Hashes below identify the inspected source, rather than an unpinned README or release claim.

## Decision

Use **fearkov/3dsrecomp as a pinned, external host lifter candidate**, with **Zakuro's MIT CPU and HLE interfaces as the primary runtime port candidate**. The lifter and small CPU package both reproduce successfully on this machine. Keep generated code, guest memory and scheduling behind a C/C++ runtime boundary; compile Wii U code with WUT/devkitPPC. The first prototype must remain independently executable without a game dump.

This selection establishes a host-side foundation. It does not establish that Zakuro's Rust desktop runtime can run on Wii U, that its entire CPU/VFP behavior is correct, or that a game can be lifted into a working Wii U program. A full HLE runtime and PICA-to-GX2 backend are still implementation work. Before distributing 3dsrecomp code or its emitted ABI header, resolve the missing copyright/license-notice file described below. An external local tool invocation avoids silently vendoring an incomplete notice.

Do not adopt `mtrdrgz/3ds_recompiler`, `nuspacker`, or `CNUS_Packer` implementation until an applicable license grant is identified. Public source access is not permission to redistribute it. Azahar is a useful semantic reference and an explicitly GPL-covered alternative; incorporating its implementation would require a compatible project licensing/distribution decision.

## Source and reuse matrix

The v0.22.0 scene increment additionally checks shader encodings against the
unmodified nihstro headers at `f4d8659decbfe5d234f04134b5002b82dc515a44`,
BSD-3-Clause, copyright 2014 Tony Wasserka. The original test adapter runs in a
separate test executable; external headers/code are never copied, linked into
the runtime or bundled with artifacts. Azahar's pinned PICA register/loader
descriptions remain a semantic reference only. Its approximate float24 arithmetic
does not establish hardware accuracy; the new runtime subset uses exact MOV
values and explicitly rejects arithmetic/rounding. See [software scenes](software-scenes.md).

| Candidate | Inspected commit | License evidence | Decision and build requirements |
|---|---|---|---|
| [fearkov/3dsrecomp](https://github.com/fearkov/3dsrecomp) | `f78cd3777a676a7c02538ab993ca574a6d843416` | Root and ABI `Cargo.toml` explicitly declare `MIT`; no `LICENSE`, copyright, or complete permission notice is present in this checkout. | External host lifter candidate. Rust manifest minimum 1.88, tested with 1.95.0; C compiler required for emitted C. Linux/Windows GCC or Clang paths exist. Optional `verify` adds Zakuro CPU and dynamic library loading. Complete redistribution notices before bundling. |
| [fearkov/zakuro](https://github.com/fearkov/zakuro) | `76f17bf76cd9ea238a94f37d389fe701ab05a673` | Root MIT `LICENSE`, copyright 2026 fearkov; package manifests declare MIT. | CPU/HLE port candidate. Rust 1.95 or newer. Small `zakuro-cpu` build is practical; whole Linux desktop build also needs ALSA/udev development files and desktop GPU/audio/window dependencies. |
| [mtrdrgz/3ds_recompiler](https://github.com/mtrdrgz/3ds_recompiler) | `600f22cf7bf02a478b6455a971c8e27f7c742860` | No license file, SPDX notice, copyright/license grant, or package license declaration found in tracked source. | Do not vendor/adapt. Python standard-library lifter; runtime C++20/CMake ≥3.16, threads, optional SDL2/WebGPU. Unicorn/Capstone are differential-test dependencies. Source inspection only; no runtime build claimed. |
| [devkitPro/wut](https://github.com/devkitPro/wut), including [samples](https://github.com/devkitPro/wut/tree/cc35feb58d3c3bff9af898ab3df6965e6abcf139/samples) | `cc35feb58d3c3bff9af898ab3df6965e6abcf139` | Root zlib `LICENSE.txt`, copyright 2015-present devkitPro, wut Authors; samples have no contrary license notice found. | Selected Wii U SDK. Prefer devkitPro Pacman `wiiu-dev` released packages. `DEVKITPRO`, `DEVKITPPC`, WUT, GNU Make or devkitPro CMake helpers, and tools including `elf2rpl` are required. |
| [azahar-emu/azahar](https://github.com/azahar-emu/azahar) | `d409087405bc299b88ff35dd5b96b6e2698d5929` | Root GPLv3 `LICENSE.txt`; individual files use GPLv2-or-later and GPLv3-or-later notices, including GPLv3-or-later SVC code. Submodules have separate licenses. | Semantic reference; no implementation copied. Whole build requires CMake ≥3.25, C++20, threads and many submodules. It is a large desktop/mobile emulator, not an existing Wii U backend. |
| [ihaveamac/nuspacker](https://github.com/ihaveamac/nuspacker) | `797b84af32e5bb0c353577a7a3f67e236f7b7ee3` | No source license grant found; bundled JAR and certificate material lack an identified redistribution grant. | Packaging research only. Java/JDK required; bundled class version is Java 8. Source-only `javac` attempt failed because `javac` is absent. |
| [NicoAICP/CNUS_Packer](https://github.com/NicoAICP/CNUS_Packer) | `85c31c1ea6b98afa8037ec429da39a92e09625f4` | No source license grant found. | Packaging research only. Targets obsolete `netcoreapp3.0`, with preview trimming package; Windows-specific paths and publishing configuration need review. Source-only `dotnet build` attempt failed because `dotnet` is absent. |

WUT's SDK license does not replace the licenses of GCC, newlib, build tools, or optional port libraries. Toolchain redistribution needs a separate package-by-package notice/source audit. No Nintendo proprietary SDK, donor Virtual Console title, commercial ROM assets, or keys were acquired for this audit.

## Reproduced host results

Initial machine inventory: Python 3.12.14, GCC/G++ 14.2.0 and GNU Make 4.4.1 available; Rust, Cargo, CMake, Clang, Javac and .NET were absent; a Java runtime was present. Rust 1.95.0 was installed under `/workspace/ctr2wup-tools` with official HTTPS rustup artifacts. The official `rustup-init` SHA256 was verified before execution; rustup's toolchain checksum verification remained enabled.

| Check | Actual result | Wall time | Peak child RSS |
|---|---|---:|---:|
| `cargo +1.95.0 build --locked --release` in 3dsrecomp | Exit 0; real host CLI produced | 6.133 s | 352,576 KiB |
| `cargo +1.95.0 test --locked --release --workspace` in 3dsrecomp | Exit 0; 48 unit tests and 1 doctest passed | 6.903 s | 370,220 KiB |
| `cargo +1.95.0 test --locked --release -p zakuro-cpu` in current Zakuro | Exit 0; 49 ARM, 21 Thumb, 22 VFP tests passed | 13.839 s | 300,588 KiB |
| 3dsrecomp `analyze` on an original synthetic NCCH fixture | Exit 0; 4 ARM instructions, 1 function, 1 SVC discovered | ~0.001 s | Not measured |
| 3dsrecomp `build` on that fixture | Exit 0; generated C and real native `.so` | 0.048 s | Not measured |
| GCC build and execution of the emitted C with a small C ABI harness | Exit 0; `r0=43`, SVC 3, preserved resume PC, then `BX LR` returned to the specified address | Not measured | Not measured |

The fixture contains `MOV r0,#42; ADD r0,r0,#1; SVC #3; BX lr` authored for this audit, not commercial code. Its executable bytes have SHA256 `3d790bac40f2ca8fc8c0ce75c0a79f70c3ed42a0c0078a9d85626fb306c4ab48`. It is a minimal parser/lifter/ABI test fixture, not a complete bootable 3DS homebrew program. It does not exercise the guest kernel or graphics.

Build logs/results remain at `/workspace/ctr2wup-tools/audit-builds`; the real CLI is `/workspace/ctr2wup-tools/3dsrecomp-target/release/3dsrecomp`. Cargo lockfiles and candidate tracked files remained unchanged. The 3dsrecomp lockfile resolves its optional verification CPU to Zakuro commit `3e853d5a091dab9b7bfb3b0f41e3457d42ff4bb6`, which differs from the current Zakuro checkout audited above. The optional verification feature was not executed, and no differential correctness claim is made for it.

Reproduce with the pinned checkouts, then:

```sh
export RUSTUP_HOME=/workspace/ctr2wup-tools/rustup
export CARGO_HOME=/workspace/ctr2wup-tools/cargo
export PATH="$CARGO_HOME/bin:$PATH"
export CARGO_NET_GIT_FETCH_WITH_CLI=true
cd /workspace/ctr2wup-deps/3dsrecomp
CARGO_TARGET_DIR=/workspace/ctr2wup-tools/3dsrecomp-target cargo +1.95.0 build --locked --release
CARGO_TARGET_DIR=/workspace/ctr2wup-tools/3dsrecomp-target cargo +1.95.0 test --locked --release --workspace
cd /workspace/ctr2wup-deps/zakuro
CARGO_TARGET_DIR=/workspace/ctr2wup-tools/zakuro-cpu-target cargo +1.95.0 test --locked --release -p zakuro-cpu
```

These are x86-64 Linux results. They are neither PowerPC timings nor proof of Windows or physical-console execution.

## CPU and 32-bit big-endian portability

### 3dsrecomp

The host lifter has real ARM, Thumb and VFP emitters, interpreter callbacks for unsupported instructions, an instruction budget and SVC exits, module-aware discovery/relocation analysis, and generated code identity checks. `src/lib.rs` exposes generation separately from compilation; `build::Options` can select a compiler. `src/compile.rs` defaults to desktop shared-library flags and must be adapted for RPX/static linking rather than assuming `port` outputs a Wii U project.

Guest registers/addresses are fixed-width `uint32_t`. The C ABI uses host page pointers rather than casting guest addresses directly to host pointers; this is a useful boundary, but its current fast path is **not big-endian-safe**. In `abi/recomp.h:168–206`, 16/32-bit guest loads and stores use native-endian `memcpy`. The generated multiple-transfer span path in `src/codegen/mod.rs:81–120` calls `load32`/`store32`, which also use native-endian memory representation. Merely fixing fallback callbacks will leave mapped fast-page and span accesses wrong. A Wii U adapter must replace the guest-word helpers or disable those fast paths and provide explicit little-endian callbacks.

VFP double bit assembly in the C header numerically joins low/high words before bit copying, which avoids one common big-endian lane mistake. Floating arithmetic still uses host `float`/`double`. The emitter explicitly leaves some conversions to the interpreter, and compiler flags disable FMA contraction, but exhaustive FPSCR rounding, NaN/denormal, signed-zero, exception and conversion correctness is unproven. Exclusive monitors, invalidation, aliases, page permissions, ABI offsets and 32-bit pointer sizes need targeted checks. The existing C/Rust layout test only ran on this host.

### Zakuro

`zakuro-cpu` is a small, useful interpreter candidate with a `Bus` interface and `u32` guest state. `zakuro-core/src/memory/mod.rs:627` uses explicit little-endian guest reads/writes. `zakuro-cpu/src/vfp.rs:49` constructs double bit patterns from numeric low/high words. Those design choices fit a portable runtime better than a flat host address reservation.

The whole runtime is substantially less portable: `zakuro-core/Cargo.toml:14` unconditionally enables Vulkan; GPU code uses `wide::f32x8`, Rayon, native worker threads and desktop window/GPU loading. Raw page pointers and unsafe `Send` appear in `memory/mod.rs:78`; `recompiled.rs:260` passes pointer-backed page tables through the C ABI. Wii U thread, allocator, filesystem, timing, renderer and static-link boundaries must be supplied. No devkitPPC-compatible Rust target/runtime has been validated.

VFP operations use native `f32`/`f64`; upstream explicitly leaves strict default-NaN propagation for future work in `vfp.rs:639`. Ninety-two passing host instruction tests are useful regression evidence, not a complete ARMv6K/VFP oracle. Reuse the test ideas and compare against trusted references, then run on actual big-endian PowerPC before allowing broad instruction support.

### mtrdrgz/3ds_recompiler

The source contains a Python ARM/Thumb/VFP lifter, C++ HLE runtime, interpreter fallback, software PICA renderer and desktop/WebGPU paths. Important source hazards are independent of its license blocker: `rt/mem.h` reserves 4 GiB on native hosts and maps guest VA by `g_base + va`; read/write helpers use native-endian `memcpy`. A 32-bit Wii U address space cannot provide that native 4 GiB reservation. WASM selects a different 512 MiB path, which does not establish PPC support.

The shader fast path uses `__m128`/SSE2; ARM64 builds use `sse2neon`, not PowerPC. Host FP control mappings target x86/ARM64, with an alternate software path requiring separate validation. Kernel scheduling creates a host thread per guest thread. README acknowledges missing dynamic CRO loading, services, and generic-success service stubs. Its author reports a playable retail title on desktop/Web; that result was not reproduced here.

### Azahar

Azahar is a broad emulator and useful HLE/PICA semantic reference, but its documented host platforms are 64-bit x86-64/ARM64 desktop/mobile systems. `src/core/CMakeLists.txt:552` selects Dynarmic only for x86-64/ARM64; GCC/Clang generic builds choose computed-goto FastInterp. This does not establish PowerPC correctness.

Concrete big-endian hazards remain in FastInterp: `fastinterp.cpp:642–695` copies guest bytes directly to native 16/32-bit words and back; `handlers/arm_vfp.inc:18–26` copies adjacent low/high `u32` words into a native double. Azahar's normal memory access and endian wrappers do not fix these bypass paths. GPL-covered implementation reuse must preserve applicable notices and corresponding-source obligations; no such source has been copied into CTR2WUP.

## WUT and sample suitability

WUT directly targets Espresso/PowerPC EABI with hard float (`share/wut_rules`: `-mcpu=750 -meabi -mhard-float`) and exposes OS, GX2, VPAD, audio and RPX/RPL support. This is the appropriate native console SDK candidate. The sample source demonstrates orderly WHB startup/shutdown and actual GX2 APIs rather than emulated desktop equivalents. `samples/cmake/gx2_triangle` expects `pos_col_shader.gsh` in SD content; its presence is a sample dependency, not evidence that arbitrary PICA shaders can be compiled for Latte.

SDK/library source review does not itself produce a valid RPX. The platform prototype's separate build report records the actual toolchain setup and compile result. Physical Wii U dual-display, touch, audio and exit acceptance remains awaiting console tests.

## Packaging investigation

Both audited NUS packers require staged Wii U `code/content/meta`, package metadata and local key configuration. Neither is a direct `.3ds` converter. `nuspacker/README.md:5` explicitly requires patched IOSU; packaging does not make a title installable on an unmodified retail console.

The implementations create TMD/ticket/content/hash structures, but do not generate authentic Nintendo signatures. Java/C# TMD code uses zero RSA signature fields; ticket code uses random signature bytes. Content and H3 hashes concern content integrity and do not establish authenticity or installer acceptance. Certificate templates also need independent provenance/license review.

Both upstream entry points echo supplied encryption keys (`Starter.java:186–187`, `Program.cs:188–189`); never pass secrets through logged commands. A tracked Java `encryptKeyWith` placeholder exists, but no credential value was printed, adopted or copied. Keep keys in user-managed local configuration, redact diagnostics, and obtain only credentials actually required by the selected future packer.

Actual source-only build errors: `javac: command not found` and `dotnet: command not found`, both exit 127. No packer executable was run, no WUP was produced or installed by this audit, and no source license gate was bypassed. Continue with an original structural validator and staging/reporting support; choose a licensed packer or obtain an explicit upstream grant before integration. A structural validation pass alone must not be reported as a successfully installed title.

## Remaining gates

Resumed 2026-10-08: v0.3.0 integrates the pinned external generation driver with
the original checked ABI; v0.4.0 implements original scoped encrypted packaging
with independent decrypt/FST/file checks. cryptography 50.0.0 uses its
BSD-3-Clause grant, preserved in THIRD_PARTY_NOTICES/docs/licenses. Offline
packaging uses original synthetic credentials only; the real WUT RPX passes
staging/packing/decryption comparison. No packer candidate implementation or
certificate template was adopted. See `packaging.md` for the current contract;
installer acceptance, certificate provenance/trust and Menu launch remain gates.

- Complete 3dsrecomp's redistribution notices; validate the original packaging profile on console.
- Replace/verify little-endian fast helpers, 32-bit ABI layouts, guest permission/alias handling, exclusive access and invalidation.
- Validate ARM/Thumb/VFP semantics differentially, including unsupported-instruction failure visibility and state at SVC/yield boundaries.
- Establish a supported Wii U interpreter/HLE platform port; the desktop Zakuro runtime is not a ready-made RPX runtime.
- Implement and measure PICA-to-GX2, guest services, CRO behavior, audio and save semantics required by a controlled fixture.
- Run physical-console platform diagnostics and PowerPC benchmarks; use those measurements before choosing a commercial game revision or promising performance.
- Validate Windows installation/builds and a complete original homebrew packaging path with a suitable installer; then request the smallest specific console evidence or user-owned dump needed for subsequent milestones.


## v0.16 asset/texture layout review (October 12, 2026)

Read-only format references: GodMode9 `f3b9ec7a50b8c8459f62b87bfd7b7214b6f6bda2`
(`arm9/source/game/romfs.c` and `romfs.h`), the already pinned Azahar texture/color
sources, and Zakuro `76f17bf76cd9ea238a94f37d389fe701ab05a673` texture/format modules.
Canonical NCCH IVFC physical/logical layout and Level-3 metadata facts informed
original bounded host verification. No GodMode9 code or bundled key database was
adopted, read for key material or redistributed. Its GPL license remains in its
separate read-only reference checkout; it is not a runtime/build dependency.

The original base texture decoder has independent golden vectors and a Python
reference. A test-only Rust wrapper compiles the clean pinned Zakuro modules in
that external checkout, compares original images, and remains outside source/
delivery binaries. The known HILO channel-layout difference is documented in
renderer-status.md rather than hidden in a blanket comparison claim. No Azahar
implementation is copied or linked. These checks do not prove GX2 rendering,
real-game assets or Wii U performance.
