# CTR2WUP Development

**Product:** a Windows GUI that turns an owned 3DS game into its own installable
Wii U WUP. Select a game, configure its title/artwork, Inject, install, and play
from Wii U Menu. Super Mario 3D Land is the first candidate. Current releases are
original diagnostics; commercial gameplay, general PICA/GX2 rendering and the GUI remain
unfinished.

## Tracking authority

This Project is the source of truth for task status, blockers and full milestone
acceptance. Change **Status** and **Waiting resource** here. Issue descriptions
hold scope, prerequisites, acceptance criteria and evidence; source files hold
versioned implementation receipts. Source JSON is an evidence/initialization
record and cannot overwrite an existing Project status.

The dashboard reads this Project and publishes aggregate counts. A publication
error must leave the last successful timestamp visible; it never invents a fresh
status or turns missing evidence into completion. Task completion is separate
from the rough engineering estimate toward a playable injector.

## Delivery gates

| Gate | Deliverable and acceptance boundary |
| --- | --- |
| M1 · Wii U platform | Native displays, controls/audio, repeat launch/exit, verified SD results/save recovery, and an installed original WUP. |
| M2 · CPU translation | Required ARM/Thumb/VFP and ABI/memory behavior, bounded visible fallback, and measured Espresso execution. |
| M3 · Homebrew runtime | Original compiled guests on console, required services/modules/audio, and repeatable save/asset access. |
| M4 · PICA → GX2 | Vertex/shader/fragment execution, textures and coherency, original reference scenes, and console measurements. |
| M5 · First game | Privately identified Mario revision, boot/gameplay/audio/save/reload and measured performance. |
| M6 · Automated WUP conversion | Reproducible Windows conversion and packaging, actual installation/Menu launch, rebuild cache and save continuity. |
| M7 · Windows injector GUI | A responsive packaged Select → Configure → Inject workflow backed by the verified engine, useful diagnostics and clean-PC acceptance. |

Each gate has an overview and five focused tasks. Dependencies describe what is
needed to accept a task; independent fixtures and shared runtime development can
continue while a prerequisite awaits evidence.

## Status and review

- **Planned:** scoped work has not started.
- **In progress:** implementation exists but its criteria remain open.
- **Awaiting evidence:** an artifact is ready for the named console/input/Windows check.
- **Blocked:** a recorded prerequisite prevents completion.
- **Ready for review:** the scoped change and required checks are ready to assess.
- **Done:** a task's criteria have been reviewed; this never accepts its parent gate.
- **Closed:** work was closed without representing accepted delivery.
- **Accepted:** reserved for a full milestone overview after all its criteria and
  physical/title/Windows evidence have been reviewed. A merged PR cannot set it.

Keep the gate open until that review is complete. Archive only after preserving
the acceptance record; archived managed items make synchronization fail visibly.
Use Priority for ordering, and Waiting resource for the concrete next handoff.
Record evidence in issue comments or maintainer notes; retain failures and verbal
reports separately from versioned logs.

## Console handoff

Named TXT collection, fresh-launch Save, embedded/SD RomFS and GX/configuration
results are already reviewed. October 13 Audio Check/GX Transfers 0.20.0 and
Guest Draws 0.23.0 have scoped passes for silence, expected pictures and normal
exit. Preserve those results without repeating unchanged guides. Correct the
drawn-frame confirmation gate and profile its 57.33-second presentation delay;
full platform, commercial and Windows conversion acceptance remain open.
Original diagnostics require no ROM or key. Retain named TXT reports and observed
exit/pictures privately, with exact source and environment versions when available.
Do not publish game dumps, decryption keys, retail saves or commercial traces.

The USA Mario executable, matching preparation/inventory reports and all seven
selected payloads are verified privately. M5/intake/requirements analysis can
proceed. Broader modules, actual runtime use and full game acceptance remain open.
Source, maintained articles and aggregate tracking share
[the canonical project site](https://jdiller-86.github.io/CTR-2-WUP/).

[Requirements](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/docs/project-brief.md)
· [Acceptance criteria](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/docs/milestones.md)
· [Build evidence](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/STATUS.md)
· [Development releases](https://github.com/Jdiller-86/CTR-2-WUP/releases)


Native feature/task/PR relationships and their live verification are documented
in [development tracking](https://jdiller-86.github.io/CTR-2-WUP/docs/development-tracking/). The initial reviewed status
migration only replaces old empty/Todo values; later owner edits are preserved.
