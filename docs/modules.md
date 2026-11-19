# Packaged CRO modules

The portable runtime supports bounded CRO0/CRS loading, relocation, linking,
calls and unloading through `ldr:ro`. A package supplies immutable input bytes,
fixed guest load/data/BSS addresses, relocated code witnesses and original AOT
blocks. This is an initial module implementation, not general commercial module
compatibility or CRR signature authentication.

## Prepare and attach

```sh
python3 -m translator.modules private/modules/package.json --output private/modules/generated
```

The version-1 JSON manifest contains a `modules` array. Each entry has `file` and
`address`; the one CRS entry has `static: true`. CRO entries provide
`data_address`, `bss_address` when needed, and instruction `ranges` with `offset`,
`size` and optional `thumb`. Offsets are relative to the CRO code section;
explicit ranges distinguish instructions from literal pools. Files are relative
to the manifest. Commercial input, generated code and reports stay private.
See the original [fixture manifest](../tests/fixtures/modules/package.json).

The command produces `modules.generated.c`, its declaration header and a report.
Compile the generated C with the shared runtime and call
`ctr_modules_configure(guest, ctr_module_package, ctr_module_package_count)` before
execution/service access. Package storage must outlive the guest. Preparation
publishes output only after every module translates; invalid ranges, unsupported
relocations and unresolved/ambiguous imports fail without publishing output.

Main code may use the interpreter, original AOT or checked external AOT adapter.
The external adapter dispatches packaged module blocks through the same original
helpers, preserving budgets and callbacks into main code. Its external-emitter
module tables remain unsupported. Guest source buffers must exactly match a
configured file; linked executable bytes must match its relocated code witness.
Loading bytes cannot create host code.

## Supported contract

- Files are page padded, with declared file size equal to supplied size; fixed/
  cropped files are unsupported. Each package has one CRS and up to seven CROs,
  at most 4 MiB per file, within the shared 64 MiB retained-memory budget.
- Up to four distinct code, read-only, data and BSS segments are supported. CRS
  addresses refer to existing main mappings. CRO code/read-only segments use
  owned file bytes; data/BSS use checked configured heap buffers. Metadata
  offsets and segment tags are explicitly rebased.
- Named, module-indexed and module-anonymous imports resolve against loaded,
  linked providers. Duplicate/missing providers or symbols fail. Providers load
  before consumers; later loads do not silently repair a previous failure.
- Relocations support NONE (0), ABS32 (2), REL32 (3) and TARGET1/absolute (38),
  checked aligned destinations and explicit addends. Batches must terminate,
  have one import owner and cover every external relocation. Duplicate targets,
  invalid tags and unowned records fail. Branch relocations and special static
  anonymous/static relocation forms are unsupported.
- Each import/export table is bounded to 128 records and each internal/external
  relocation table to 1,024. Export lookup is checked linear lookup; supplied
  tree records are structurally validated. ARM/Thumb16 instruction ranges use
  the original translator's supported instruction scope.

`Initialize` (1), `LoadCRO`/`LoadCRO_New` (4/9), `UnloadCRO` (5), `LinkCRO` (6),
`UnlinkCRO` (7) and `Shutdown` (8) accept checked current-process copy-handle
requests. SRV exposes `ldr:ro` only after package configuration. Its process-local
registry survives session closure until unload/shutdown or guest close.

Deferred/unlinked modules are non-executable. Linking resolves imports and
validates code before execution. Unlink disables execution and clears provider
dependencies. Entry, constructor/destructor and unresolved-symbol calls remain
guest-owned. Callers must quiesce module call stacks before unlink/unload. The
CRS registration lists expose the bounded auto/manual module sets.

Only fix level 0, zero reserved arguments and configured addresses are supported.
CRR registration/authentication, arbitrary relocation bases, cross-process
loading, general SDK autolink/lazy resolution and retail module discovery are
unimplemented. Unsupported commands/shapes fault visibly; malformed files,
ownership failures and unresolved imports return nonzero RO results.

## Ownership and failure behavior

Load stages the file, initialized data, zeroed BSS, relocations and permissions
before publication. Failure preserves guest bytes, mappings, pins and generations.
DATA/BSS allocations stay pinned while loaded and cannot be freed/reprotected
through ControlMemory. Executable file pages are read/execute; other file pages
are read-only. Consumer dependencies and active CPU/thread PCs prevent provider
unlink/unload. LR alone is not a live-call proof: ARM callers may retain an old
LR after returning.

Unload removes owned mappings and translated lookup ownership, releases pins and
frees the file. Stale pointers cannot authorize unloaded blocks. Guest close
releases all modules before heap backing. Unknown file-backing aliases prevent
replacement/free. Only successful loader changes refresh unchanged AOT
generations; unrelated executable writes retain stale-code failure.

## Automated validation

```sh
python3 -m unittest tests.test_module_package
make -C tests/modules check sanitize
python3 tools/validate-upstream.py --only modules --sanitize --output artifacts/module-aot
make prototype
```

The original guest loads a CRS and two modules, resolves named/indexed/anonymous
imports, calls main ARM code and module ARM/Thumb code, stores a BSS result,
links/unlinks, rejects missing providers and busy unloads, verifies heap pinning,
unloads and shuts down. Interpreter and AOT compare registers, instruction counts
and mapped bytes at uneven budgets, excluding only the synthetic clock sequence
count. Focused cases cover malformed relocations, identity mismatch, executable
invalidation and stale entries. These checks require no manual console testing;
physical performance and actual-title module use remain unverified.

Wire layout and RO command facts were checked against the existing semantic
research pin [Azahar d409087](https://github.com/azahar-emu/azahar/tree/d409087405bc299b88ff35dd5b96b6e2698d5929/src/core/hle/service/ldr_ro).
Implementation and fixtures are original CTR2WUP code.
