# Shared TLS and SRV/CFG startup support (0.6.0)

This is a reusable runtime foundation for the planned GUI injector. It needs no
commercial ROM. An original ARM program exercises standard TLS/SVC/IPC interfaces
and reads configuration through the same C runtime used in the Wii U builds.
It does not establish conventional homebrew boot or commercial compatibility.

## Supported interfaces

ARM `MRC p15,0,Rd,c13,c0,3` reads the current CPU's user read-only TLS address.
The interpreter, original translator and external ABI agree, including condition
skips and instruction budgets. Rd=PC, writes, other CP15 registers and cache
operations are unsupported. The external adapter refreshes TLS at every execution
entry; an attach-time value cannot become stale after the thread context changes.
This does not implement thread creation/scheduling or hardware cache maintenance.

| Interface | Exact request | Behavior |
|---|---|---|
| SVC ConnectToPort `2d` | `r1`: checked NUL-terminated `srv:` name | `r0`: result; `r1`: generation-tagged SRV session handle |
| SVC SendSyncRequest `32` | `r0`: session handle; command at TLS+`80` | Synchronous validated request/reply; SVC result is separate from service result |
| SRV RegisterClient | Header `00010002`, current-PID descriptor `20`, one placeholder word | Records registration for this session; reply `00010040`/result0 |
| SRV GetServiceHandle | Header `00050100`, name[8], length, flags0/1 | `cfg:u` and, from v0.9, `hid:USER`; reply `00050042`, result, move descriptor `10`, handle |
| CFG SecureInfoGetRegion | Header `00020000` | Reply `00020080`, result, configured region |
| CFG GetSystemModel | Header `00050000` | Reply `00050080`, result, configured Old-family model |
| CFG GetModelNintendo2DS | Header `00060000` | Reply `00060080`, result, 0 for Old2DS and 1 otherwise |
| CFG GetConfigInfoBlk2 | Header `00010082`, size1, language block `000a0002`, mapped descriptor `1c`/`1e`, guest output pointer | Writes one language byte; returns `00010042`, result, descriptor/pointer |

`ctr_guest_configure` accepts region0..6, language0..11 and Old-family models
0 (Old3DS), 1 (Old3DS XL), 3 (Old2DS)
before execution/service access. Diagnostic defaults are USA1, English1, Old3DS0.
These are explicit emulated settings; they are not read from the physical Wii U
or inferred from an uninspected ROM. Future injection profiles must supply their
chosen settings. New-family models 2, 4 and 5 remain outside this execution
profile. The libctru enum corrects swapped 2DS/New3DS IDs in a Zakuro comment.

Unavailable service names return a permanent SRV NotFound result `d88067fa`,
without allocating a session. This bounded directory does not wait for services
to register asynchronously, even for flags0. It permits library fallback from
absent `cfg:i`/`cfg:s` to `cfg:u`; those privileged interfaces are not advertised.
Unknown commands, unsupported config blocks and malformed request layouts fault
with the endpoint/header recorded. There are no generic-success service stubs.
Notifications, subscriptions, user/server IPC, arbitrary descriptors, process
identity/permission isolation and cross-process mappings are unimplemented.

## Bounds and ownership

The transport checks little-endian words, reserved header bits and total word
counts within the 64-word (`100`-byte) command buffer. It preflights read/write
permissions for the whole buffer before registration, allocation or replies.
Mapped language output is checked before any write; adjacent bytes remain intact.
Read/write descriptors require both permissions. Output cannot overlap or alias
the command buffer's backing storage, even through a different guest mapping.
Unknown handles and event handles cannot be used as sessions. Session handles
share the existing 64-slot generation table, with 16 reference-counted session
objects alongside the separate event pool. Duplicate/close/stale handles and
allocation rollback are tested. Closing SRV does not destroy acquired CFG sessions.

Failures retain IPC endpoint, requested directory target, raw header, result and
request/failure counts. Those diagnostics are available in the runtime and SD
log. They do not imply that a service in a ROM's allowlist was actually executed.
The runtime still owns one CPU/process; this is not a complete 3DS kernel.

## Reproduction and original program

```sh
python3 tools/generate-service-fixture.py
make -C tests/services check sanitize
python3 tools/validate-upstream.py --output artifacts/my-service-integration
```

The tracked probe is generated from `host/service_fixture.py`, not a game dump.
It reads TLS, connects/registers SRV, opens CFG, reads region/model/language,
closes both handles and exits: 89 executed ARM instructions, five IPC requests
and nine SVCs. A private synthetic NCCH built from those bytes also passes the
normal checked loader and real pinned external lifter. The CLI commercial profile
gate and framebuffer fixture profile remain unchanged.

The C service suite covers wire replies, configured values, duplicate/stale/wrong
handles, malformed headers/descriptors, permissions, missing services and both
session/handle exhaustion. Its interpreter/original-AOT probe matches complete
guest state and memory at uneven quotas. External integration compares the same
program through all three paths, including a TLS address changed after attach.
A separate CP15 probe prevents cache operations from disappearing as emitter NOPs.
It also checks unsupported DMB/DSB/ISB barriers instead of silently accepting them.
`--service-api` adds the full malformed-input suite to an external validation run.

All three Wii U diagnostic builds embed the original interpreter/AOT startup
self-test. Native mode displays `TLS/SRV/CFG STARTUP PASS CODE 0` after success;
the SD log records `service_selftest`. Physical console results remain required.
Emulated big-endian PowerPC Linux results do not establish Wii U execution/speed.

Final v0.6.0 reports: `validation-v1-20261008T135953Z/report.json` (135 Python
tests; 27 full checks; 1,001 service API assertions normally and with sanitizers),
`services-clang-final-v6/report.json`, `services-ppc-final-v6/report.json` (10
checks each, including 566/34/297/35 integration assertions and the service API).
All report paths are under `artifacts/reports/`.

## Interface sources

No upstream implementation was vendored. Interface facts were checked against:

- devkitPro/libctru `9b55eda44cf80b971503991e0c78f9bc8fe50425`: `source/svc.s`,
  `source/srv.c`, `source/services/cfgu.c`, `include/3ds/ipc.h`,
  `include/3ds/services/cfgu.h` and thread TLS helpers.
- The audited Zakuro `76f17bf76cd9ea238a94f37d389fe701ab05a673` kernel IPC/SVC
  and SRV/CFG interfaces, and the pinned lifter's CP15 emitter.
- Audited Azahar's documented result-code fields and service-result descriptions;
  no GPL implementation is incorporated.

Hardware equivalence and broader CFG/Mii/save identity behavior await the selected
Mario revision and appropriate reference/console tests. FS/APT/GSP/PICA,
multi-thread kernel behavior, audio, saves and CROs remain implementation work.
HID pad/touch support is now defined in `hid-input.md`; motion remains absent.
