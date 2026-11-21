---
name: aiverse-physis
description: Reuse AIVERSE PHYSIS for deterministic weather, wind, gusts, storms, gravity and environmental forces. Shows genre-neutral ECS integration, optional Box3D Scene forces, ownership split with Water/NEREID and TerraWeave, tests and mandatory handoffs.
---

# AIVERSE PHYSIS — shared nature and force systems

**Never reimplement PHYSIS inside an FPS/RTS/RPG module.** PHYSIS core must remain genre-independent, renderer-independent and headless. Read `docs/physis.md`, `engine/physis/AGENTS.md`, and `docs/physis-nereid-terraweave-integration.md` before editing.

## Agent authoring and current verification

For MCP map configuration use `skills/world/aiverse-physis-authoring/SKILL.md`:
`physis_get`, `physis_validate`, `physis_set`. They edit authored documents only.
For physical sky, volumetric clouds/god rays and sphere/OBB fog/reveal, use
`skills/visuals/aiverse-prisma/references/atmosphere.md`; PRISMA owns rendering.

Historical NOT RUN notes below are stage-local. Latest scoped evidence is in
`docs/agent-handoffs/physis-stage107-qa-fixes-2026-11-20.md` (30+2 native tests)
and `docs/qa/physis-gui-resume-2026-11-20.md` (demo ticks 25→26→86→25).
Neither certifies full live-host or reference-image visual acceptance.

## Ownership

- **PHYSIS:** atmospheric/weather climate, wind/gust/shelter, storms (hurricanes/tornadoes), snow/wetness/ice, fire/quakes, configurable gravity and environmental force **policy**.
- **NEREID water agent (PR #23):** canonical lakes, rivers, oceans, flow/current, water surfaces and buoyancy. Set `InstallOptions::floodMode = FloodMode::ExternalAuthority` when NEREID owns water. PHYSIS rain is normalized intensity, **not** metres of water.
- **TerraWeave agent (PR #24):** land geometry, heightfield, biome, foliage, vegetation and ground presentation. Terra consumes immutable PHYSIS/NEREID snapshots; PHYSIS must not generate terrain or run a second water solver.
- **Box3D Physics:** movement/collision/forces and physics snapshots. Use the existing `AIVERSE::physics` target and `physics::Scene`; only the optional bridge under `engine/physics/PhysisBox3D.h` may include Box3D types.
- **Renderer/VFX:** consumes presentation snapshots only; never controls authoritative climate.

## C++ API map

| Feature | Header | API |
|---|---|---|
| Core grid and ticking | `engine/physis/Physis.h` | `initialize`, `advance`, `cellAtPosition`, `sample` |
| Climate and weather fronts | `PhysisClimate.h` | `ClimateZone`, `WeatherFront` |
| Surface wetness / snow | `PhysisEnvironment.h` | `environmentAt`, `EnvironmentalResponse`, `advanceEnvironment` |
| Fire/quake/shallow-flood prototype | `PhysisDisasters.h` | `DisasterConfig`, `FloodMode`, `ignite`, `hazardAt` |
| Spatial 3D wind and gusts | `PhysisWind.h` | `WindZone`, `WindShelter`, `windAt` |
| Uniform/point gravity fields | `PhysisGravity.h` | `GravitySettings`, `GravityField`, `gravityAt` |
| Hurricane/tornado vortex fields | `PhysisStorm.h` | `StormEvent`, `StormKind`, `windWithStormsAt`, `stormsAt` |
| Shared drag/gravity force policy | `PhysisForces.h` | `ForceReceiver`, `ForceOutput`, `evaluateForces`, `advanceForces` |
| Combined read-only 3D climate + hazard + force fields | `PhysisPresentation.h` | `NatureFieldSample`, `sampleNatureField` |
| Read-only raster for editor diagnostics | `PhysisDiagnostics.h` | `DiagnosticRequest`, `DiagnosticRaster`, `sampleDiagnosticRaster` |
| Snapshot spatial broadphase (optional) | `PhysisSpatialIndex.h` | `FieldSpatialIndex`, `sample`, `stats`, `candidateCount` |
| Indexed diagnostics (optional) | `PhysisDiagnostics.h` | `sampleDiagnosticRasterIndexed` |
| Debug palette (non-authoritative) | `PhysisDebugPalette.h` | `DebugColor`, `debugColor`, `diagnosticColor` |
| Dockable editor debug | `apps/editor/windows/PhysisWindow.cpp` | `AIVERSE_EDITOR_WINDOW(PhysisWindow)`, read-only live / private demo |
| Generic native host | `PhysisIntegration.h` | `InstallOptions`, `install`, `forEachPresentationCell` |
| Fire/wind presentation frame (host) | `engine/world/PhysisNaturePresentation.h` | `buildNaturePresentation`, `NaturePresentationFrame::sample/signature` |
| Fire FX / audio planners (host) | `engine/world/PhysisNatureCues.h`, `PhysisNatureFxHost.h`, `PhysisNatureAudioHost.h` | `PhysisNatureFxHost`, `PhysisNatureAudioHost`, `addPhysisNatureSounds` |
| TerraWeave grass burn + wind (host) | `engine/world/PhysisNatureGrass.h` | `applyPhysisGrassPresentation`, `applyPhysisGrassWind` |
| Editor live nature preview | `apps/editor/PhysisNaturePreview.h` | PHYSIS window «Живая природа», MCP `physis_nature_preview` |
| Optional Box3D consumption | `engine/physics/PhysisBox3D.h` | `applyPhysisToBody`, `PhysisBodyMaterial` |

Install PHYSIS once from `GameModule::setup` before the persistence registry seals. `InstallOptions.enableStorms`, `enableForces`, `enableWindZones`, and `enableGravityFields` default false, so existing games retain their behaviour. PHYSIS updates precisely once per authoritative `Simulation::step` (never once per render frame); the optional force pass **only publishes** ECS outputs and does not move bodies.

## Persistence invariants

Existing Stage 1–5 native save schemas must not change by default. `enableWindZones` registers WindZone/WindShelter, `enableGravityFields` registers GravityField/GravitySettings, `enableStorms` registers StormEvent, and `enableForces` registers ForceReceiver/ForceOutput. Set the flags consistently before saving/loading/replaying; the registry rejects mismatched schema manifests. The new `sampleNatureField` is a read-only adapter used by Terra, VFX, editor and physics consumers, not a second world state.

## Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h"

// In generic native GameModule::setup:
aiverse::physis::InstallOptions p;
p.enableStorms = true;
p.enableDisasters = true;
p.floodMode = aiverse::physis::FloodMode::ExternalAuthority; // when NEREID owns water
aiverse::physis::install(sim, p);

// Physics owner thread, after authoritative PHYSIS update:
for (auto body : optedInDynamicBodies)
    aiverse::physics::applyPhysisToBody(scene, sim.world(), body);
scene.step();
```

Link `AIVERSE::physics` and enable `AIVERSE_ENABLE_PHYSICS=ON` only in opt-in native hosts. The bridge samples the actual Box3D body mass/velocity/position and applies quadratic wind drag plus **mass × (PHYSIS gravity − Box3D built-in gravity) × gravityScale** with `Scene::applyForce` before `Scene::step`; otherwise gravity would be counted twice. Do not apply `ForceOutput::totalForce` to the *same* Box3D body. `physics::Character` retains its separate scalar-gravity controller and is **not** modified by this adapter. Box3D float solver does **not** become lockstep-deterministic because PHYSIS uses fixed point.

## Current status and tests

Implemented fields are simple authored wind/vortex/gravity policies; they are **not** CFD weather, N-body gravitational physics, full destruction, water storm surge, GPU storm effects or foliage wind deformation. No automatic NEREID coupling is claimed; the opt-in nature host (2026-11-21) couples fire/wind to TerraWeave grass, SpectraFX and audio — see `docs/agent-handoffs/physis-nature-host-2026-11-21.md`. Native compilation and tests are **NOT RUN** for connector-authored stages, so keep PR #25 draft until validated.

```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

For Box3D integration also run `physics_runtime`, `physics_scene`; for shared water/terrain run NEREID/Terra and legacy regressions. Do not claim PASS unless logs demonstrate it.

## Mandatory agent handoff

**Every implementation stage** must commit its code/tests, then a **separate documentation commit** to `docs/agent-handoffs/physis-<stage>-YYYY-MM-DD.md` with branch, exact implementation SHA, paths, public API, actual test PASS/FAIL/NOT RUN results, known limitations and next explicit task. Update root `AGENTS.md`, `engine/physis/AGENTS.md` and `docs/physis.md` when changing ownership/contract. Do not commit into another agent's active Water or Terra branch; no force pushes.

## Optional many-query acceleration (Stage 15–16)

For temperature/humidity/rain/snow/fire/quake diagnostic heatmaps, call
`sampleDiagnosticRaster`; scalar layers now read relevant ECS state directly
without traversing every wind/storm/gravity field.

For large numbers of **3D** wind/gravity/storm queries in one committed tick:
```cpp
#include "engine/physis/PhysisSpatialIndex.h"
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::FieldSpatialIndex spatial(sim.world(),
    {.tileSizeMeters=256, .maxTilesPerVolume=1024});
aiverse::physis::NatureFieldSample data;
bool found = spatial.sample(queryPosition, data);
aiverse::physis::DiagnosticRaster raster;
aiverse::physis::DiagnosticRequest req;
req.layer = aiverse::physis::DiagnosticLayer::GravityMagnitude;
aiverse::physis::sampleDiagnosticRasterIndexed(spatial, req, raster);
```

`FieldSpatialIndex` copies the authoritative ECS World using the engine's
COW/snapshot contract and builds derived X/Z tile bins. Very large volumes and
global gravity use fallback lists; original fixed-point candidate order, storm
timing and gravity priority are retained. It is **not persisted** and must be
recreated after a PHYSIS tick, storm movement, authoritative mutation, rollback
or load if new state should be visible. Do not keep a separate mutable copy of
weather. No Box3D, NEREID or TerraWeave header is needed to use it.

This is a query-time broadphase, **not** a new streaming/grid simulation,
destruction engine or validated benchmark. Stage 16 test code checks reference
versus indexed Fixed values; actual CTest/CI PASS remains unverified until run.
See `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`.

## Editor PHYSIS debug window (Stage 17)

The standard editor auto-discovers `apps/editor/windows/PhysisWindow.cpp`:
open **Мир → PHYSIS · Природа**. Hidden by default. It reads live
`Editor::sim()` only when `physis::State` exists. The window provides
a bounded 2D heatmap, 14 selectable layers, palettes, scalar values,
wind/gravity inspection, and outlines for WindZone/StormEvent/GravityField.
It **does not modify live authoritative ECS** or game project assets.

If PHYSIS is not installed in the current game, the editor offers
**Создать / сбросить демо**: a separate `aiverse::World` with a storm, wind,
gravity and fire. Only demo buttons advance this private World. Do not
mistake the sample for a deployed game's PHYSIS integration, and do not
reuse the demo world as an authoritative server simulation.

`PhysisDebugPalette.h` provides CPU colour mapping and invalid-cell masking
for other tools without adding render dependencies to PHYSIS. Reference CTest
`physis_editor_view` covers the palette and COW diagnostic data, but the
GUI must additionally be built and manually reviewed in the real editor.
**Editor build / CTest / screenshot / click-through NOT RUN yet.**
Handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## Stage 18 Scene viewport and authored map configuration

Before changing PHYSIS editor workflows read `docs/editor-unification-handoff.md`.
The standard Scene viewport now imports `apps/editor/PhysisSceneOverlay.h`
and draws opt-in projected PHYSIS heatmap tiles/rings, read-only against the
running generic Simulation. Open **Мир → PHYSIS · Природа**, enable the
3D Scene overlay (available only when the project installed PHYSIS).
The 2D demo World is independent, and must never be written to a game save.

The common Inspector's map properties page offers PHYSIS authoring
(`apps/editor/PhysisAuthoring.h`), with structured grid/climate/feature
settings plus advanced JSON. Apply validates first, snapshots MapDoc once,
and supports Undo/Redo for both regular and FPS map document formats.
It stores a map-level `"physis"` block, but does not mutate a live World.

Native projects opt in without genre dependency:

```cpp
#include "engine/physis/PhysisProjectConfig.h"
void MyGame::setup(aiverse::Simulation& sim, const Json& rules,
                   const Json& map) {
    aiverse::physis::installFromMap(sim, map);
}
```

The loader reads grid/climate/enable flags, rejects malformed input, respects
`FloodMode::ExternalAuthority` for NEREID-managed water and registers
optional component persistence. It does **not** instantiate authored field
arrays (future explicit entity spawn adapter), nor automatically add weather
to RTS/FPS modules.

Handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`.
Actual `physis_authoring`, `physis_project_config`, `physis_scene_geometry`
CTest and interactive editor screenshot/launch: **NOT RUN**. Do not advertise
a verified UI or migrated runtime until CI/checkout evidence exists.

## Stage 19 — reusable authored wind/storm/gravity assets and Gizmos

Native project maps may author `physis.windZones`, `physis.storms`,
`physis.gravityFields`, each with a unique stable ASCII `id`, `center`
[X,Y,Z] in metres, appropriate radius and type-specific properties.
`engine/physis/PhysisFieldAssets.h` uses the SAME validation as
`apps/editor/PhysisAuthoring.h`, sorts all assets by ID and creates their
PHYSIS ECS components on explicit `installFromMap`. Nonempty collections
require `enableWindZones`, `enableStorms` or `enableGravityFields` true
to register the correct save schemas. The stable authored ID is NOT a new
persisted ECS identity and edits are not automatically hot-reloaded.

Editor: **Мир → PHYSIS · Природа** → **PHYSIS — объекты и Gizmo** can
create WindZone/Tornado/Hurricane/GravityField, select/delete/adjust typed
properties and advanced JSON. Enable `Редактировать маркеры мышью`, then
drag W/S/G markers in Scene, Shift-drag for height. The selected asset also
appears in the common MapProps Inspector. Every nontrivial drag gets one
MapDoc Undo snapshot, but never changes active game ECS directly.
See `docs/physis.md` and Stage 19
`docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`.

`physis_field_assets`, `physis_field_authoring` and the extended native
`physis_project_config` test are registered but have **NO actual CTest PASS**.
Do not state validated in-game rendering, GUI interaction or load/replay
until verified on an actual native checkout.


### Stage 21 — atomic group field authoring

Ctrl/Shift-click PHYSIS W/S/G map fields in Scene or the field list to
toggle multi-selection. Drag a group member in Move mode to translate the
whole group; Shift adjusts Y. Snap applies to the active pivot only.
Select all, clear selection, duplicate and delete work across field types.
Radius gizmo changes the active member alone.

Use physis_authoring::moveFields, duplicateFields and removeFields for
atomic map JSON batches. NEVER loop moveField per member; that would make
multiple Undo entries and allow partial invalid state. Renderer and ImGui
stay in apps/editor/PhysisFieldEditor.h, not engine/physis. No writes to
live Simulation ECS; the generic native installFromMap remains the only
explicit startup importer. Handoff:
docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.
New test fixtures are NOT RUN in CTest.


Stage 21 also provides numeric group yaw and uniform scale controls in the
PHYSIS field panel. The active field's center is the pivot. Use
physis_authoring::transformFields for one atomic Undo transaction. It
rotates positions in X/Z and uniformly scales group spacing and spatial
radii/heights; never treat it as orientation of the vector wind/force
field itself. Native validation rejects the whole group if any radius,
height or location is invalid. C++ and Scene UI tests NOT RUN.


### Stage 22 — advanced PHYSIS Scene authoring

Enable mouse marker editing for W/S/G field assets and optionally enable
rectangle selection starting from empty Scene space. Ctrl/Shift adds
projected centre markers. Pivot modes: Active, Average, Bounds Centre,
World Origin or Custom XYZ. The world X/Y/Z handles are projected for
hit testing but position changes use RTSCamera::screenRay and exact
closestRayAxisParameter. Degenerate rays must not move fields.

Rotate Y and Uniform Scale each have a horizontal projected ring.
Use frozen JSON baseline and firstEdit/touched flags on transformFields
so one mouse drag is one MapDoc Undo and every frame is computed from the
same source, not accumulated. Yaw rotates positions only; scale changes
spatial volumes and positions, not the force/velocity vectors. The
authored FieldAssets ECS types have no quaternion orientation. No live ECS
writeback or water/terrain/Box3D authority change.

All C++ fixtures added are source-only, NOT RUN. Stage 22 handoff:
docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.


### Stage 23 — stable-ID runtime reconciliation through Simulation commands

For projects that need live changes to authored WindZone, StormEvent
and GravityField assets, opt into installEditableFromMap in generic
GameModule::setup. Reserve a module command type, authorize system
commands on the server and dispatch them with applyFieldEditCommand
inside GameModule::applyCommand. A trusted host may call
submitEditableFieldsFromMap to parse/validate the full modified map
and queue its binary command through Simulation::submit/CommandSink.
Do NOT mutate the ECS World directly from an ImGui/Scene callback.

FieldRuntimeState is persisted and included in rollback snapshots.
Stable authored IDs map to Entity+kind+fingerprint; same-type updates
preserve Entity, type changes recreate, unchanged evolving storms are
not reset. Full desired field batch is sorted/validated and applied to
a staged World; wrong revision or malformed payload rejects atomically.
Existing static installFromMap remains default and save-compatible.
The opt-in adds physis.field_runtime v1 and all three component schemas
to the strict save manifest; no implicit migration.

Command wire caps: <=4096 int64 args including the length header, <=32760
payload bytes. Larger maps require an as-yet-unimplemented chunked atomic
protocol. The native host must stop client-side player=0 forgery;
there is no automatic editor registration, server ACL, GUI edit ACK or
live climate-grid/flood-policy migration in Stage 23.
See docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.
CTest added but NOT RUN.


### Stage 24 — PHYSIS editor runtime gateway

The trusted local project GameModule may implement
aiverse::editor::physis_live::FieldEditHost, return its own unique
fieldEditCommandType, explicitly allowLocalFieldEdits, install using
installEditableFromMap, then dispatch applyFieldEditCommand in
GameModule::applyCommand.

PHYSIS editor Live Editing is disabled when no opt-in host is available,
no FieldRuntimeState exists, or a network CommandSink is attached. The
editor queues validated MapDoc field updates, then observes the ECS
revision AND complete typed fingerprints for Applied. Treat Queued
as unresolved; never claim server ACK without a real remote receipt.

For authenticated server admission, use physis::admitAuthorizedFieldEdit
with a principal obtained from the authenticated CONNECTION plus the
server ACL. It rejects forged player=0 requests and constructs its own
trusted system command. No automatic remote editor transport has been
added. Do not write ECS directly from ImGui, duplicate NEREID or
TerraWeave, or bypass the genre-independent World/Command contract.
Tests authored but NOT RUN. Handoff:
docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


### Stage 25 — reliable remote PHYSIS field editing

For an authenticated editor/admin connection use the reusable
engine/physis/PhysisRemoteProtocol.h, PhysisRemoteClient.h and
PhysisRemoteServer.h on net::Conn (UDP, WebSocket or loopback).
ServerGateway::poll requires a real host-authenticated connection
principal and server ACL; call observe after Simulation steps.
GameModule::applyCommand must dispatch the Stage 23 PHYSIS command.
Never treat client player=0 or client instance IDs as permissions.

RemoteClient requests the current revision, then uses a unique
request ID with an exact original payload. After disconnect it
queries the same ID before resending; server dedupe prevents double
submission. Only Applied after actual ECS revision and fingerprint
matches is success; stale/conflicting/dropped requests get NACK.
The editor bridge apps/editor/PhysisRemoteEdit.h must receive a
host-verified Conn; the PHYSIS window exposes remote status/ACK,
refresh and safe retry.

Server cache is bounded and in-memory only. A lost server journal
after restart can leave an uncertain old ACK; do not claim
crash-durable exactly-once. Large maps can exceed Stage 23
32760-byte Command.args and need a later atomic batch protocol.
Do not mix the other agent's TrackedFromMap/RuntimeFieldRegistry
with editable FieldRuntimeState until reconciled.

Tests physis_remote_edit authored and CI registered but NOT RUN.
Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


### Stage 26 — PHYSIS durable admin sessions

Use the canonical FieldRuntimeState/EditableFromMap runtime path.
For a trusted local admin server, create
physis::remote::admin::LoopbackAdminEndpoint(privateJournalFile)
and call open(port, reservedPhysisCommandType, keyLookup, readAcl,
writeAcl, error), then poll(authoritativeSimulation, nowMs) and
afterTick(authoritativeSimulation) each committed tick.
Restore World/checkpoint BEFORE FileJournal attachment and open
the listener only when checksummed journal loading succeeds.
The operator must exclusively own the journal path.

For native editor, the trusted host may call
editor::physis_remote::link().connectLocalAdmin(
    "ws://127.0.0.1:port", principal, keyFromOSSecretStore,
    cryptographicallyRandomClientInstance, assignedPlayerSlot,
    reservedPhysisCommandType). The editor UI polls handshake
completion and existing RemoteClient receipt ACK/NACK. Secrets are
never sent; fresh nonce + keyed BLAKE2b validates the principal.
Native AIVERSE currently does not implement WSS, so this endpoint
is loopback only. Off-machine use needs encrypted transport/tunnel.

FileJournal preserves exact original request and authenticated
principal, expected revision, desired field fingerprints, scheduled
tick and status. It persists intent before enqueue, and fails
closed on write/corruption. After server restart recheck persisted
World; unknown outcomes must remain Unknown/Conflict rather than
being reported as definitely Applied. The separate World save +
journal is not a crash-atomic transaction. Do not claim exactly-once
receipt under power loss without a joint checkpoint.
Do not mix other agent's TrackedFromMap/FieldCommandHost with
canonical editable runtime. Keep NEREID, TerraWeave, Box3D separate.

Tests physis_admin_journal authored and CI registered but NOT RUN.
Handoff docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### Stage 26 queued-receipt restart recovery

When restoring a FileJournal into ServerGateway, persisted Queued
requests are now treated as Unknown: the old process may have lost
its pending command despite its durable sidecar receipt. Restore
the matching authoritative World checkpoint FIRST. Calling observe
with current FieldRuntimeState identifies already-Applied commands
by expected revision plus exact authored fingerprints. Remaining
Unknown intents may be resubmitted ONLY using the same authenticated
principal, request ID and original payload with current ACL/expected
revision checks. Do not mark stale Queued as Rejected just because
its pre-crash scheduled tick passed. This is NOT cross-file
crash-atomicity.

C++ fixtures added for crash before command execution and crash
after ECS commit before the journal Applied flush, NOT RUN.
Stage handoff docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### Stage 26 journal crash-recovery correction
A journal persisted as `Queued` may lose its actual in-memory command on restart. Restore `Queued` to `Unknown`, reconcile against restored `FieldRuntimeState`, and only certify `Applied` when revision and authored fingerprints match. If the original command was not applied, accept a retransmission of the SAME authenticated request and expected revision; never synthesize a successful receipt. The sidecar journal and World checkpoint are not one durable transaction. See `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`. Tests added, NOT RUN.

### Stage 40 — admin lock ownership and OS crash recovery

The canonical admin endpoint acquires a `JournalProcessLock` before
loading the sidecar and binding the loopback WebSocket listener. Retain
that lock until the listener and all peers have been destroyed; do not
remove or rotate `<journal>.lock` while the endpoint is alive. The lock
is process-local/advisory, **not** a distributed leadership fence.

After a server crash, re-open from a restored authoritative World
checkpoint. Every prior persisted Queued receipt must be treated as
Unknown until it is proven Applied by canonical ECS revision and
fingerprints or the same authenticated original request is safely
re-submitted under current ACL. A separate journal file and World save
are not crash-atomic.

Stage 40 source fixtures in `physis_admin_journal` cover POSIX fork,
SIGKILL, lock reacquisition and killed-endpoint Queued journal restore,
plus endpoint-to-endpoint lifetime exclusion. Windows still requires
separate process test coverage. An isolated Linux GCC 14.2 C++20
header smoke passed; full native AIVERSE CTest, macOS/Windows editor and
real external authenticated transport have **NOT** run. See
`docs/agent-handoffs/physis-stage40-cross-process-journal-2026-11-19.md`.

### Stage 41 — native validation handoff and CI discipline

Before marking PHYSIS ready, run the existing `physis.yml` job on both
Ubuntu and macOS. For each failure, inspect the uploaded
`physis-build.log` and `Testing/Temporary/LastTest.log`. The workflow
now cancels superseded runs for a single ref and preserves failures with
Bash `pipefail`. Source changes and queued jobs are **not** test passes.

When authoring crash/restart fixtures, keep a journal's `.lock` inode
alive until the final `LoopbackAdminEndpoint` destructor runs. Never
remove a temporary journal directory while a service is open. See
`docs/agent-handoffs/physis-stage41-ci-validation-2026-11-19.md`.

### Stage 42 — receipt journal failure semantics

The authenticated admin gateway may have committed a command in ECS
without persisting its Queued or Applied receipt. If
`ServerGateway::journalHealthy()==false`, outbound positive receipt
states must be masked as `Unknown`, and all fresh editor submissions
remain suspended. An unknown request ID must not be described as
"safe to retry" while dedupe persistence is compromised.

Use `tests/physis_admin_journal_test.cpp` writer-failure injection
fixture to confirm Queued persistence and post-tick Applied persistence
failures, stale receipt queries, same-ID submissions and new-ID denial.
Do not claim crash-atomic exactly-once acknowledgements: the sidecar
journal and Simulation World checkpoint are separate.

Stage 42 code/test source is committed but native compilation, CTest,
macOS and Windows QA are NOT VERIFIED. See
`docs/agent-handoffs/physis-stage42-durable-receipts-2026-11-19.md`.

### Stage 43 — PHYSIS V1 unavailable-journal client handling

Remote protocol V1 retains its exact packet format. On the server,
`Result::Unknown` with `kJournalDurabilityUnavailable` or
`kJournalRetrySuspended` means the durable receipt journal cannot
confirm deduplication. RemoteClient must enter `ClientStatus::Uncertain`,
retain the original request payload/instance/ID and **not** schedule
automatic retransmission. Manual `retry()` after recovery is a
user-initiated attempt, not proof of success.

Plain Unknown without these diagnostics retains the existing same-ID
automatic recovery behavior. Do not treat either Unknown variant as
Applied. The safety test `unhealthyJournalStopsAutomaticResubmit`
uses real net::Conn loopback and injected journal write failures at
Queued/Applied receipt persistence. This is authored code; full native
CTest remains UNVERIFIED.

Stage handoff:
`docs/agent-handoffs/physis-stage43-client-journal-backoff-2026-11-19.md`.

### Stage 44 — manual same-ID recovery after journal failure

A journal-failure `Result::Unknown` is not a transient network timeout.
`RemoteClient::disconnected()` must preserve its Uncertain status across
reconnect, and delayed Queued/Applied receipts must not reactivate
automatic retransmission. The `suspendedForJournal_` safety latch is
cleared only on a new/restored request or explicit `retry()`.

After the authoritative host repairs its journal and restores the
matching World checkpoint, the operator may explicitly call
`RemoteClient::retry()`. The client queries the SAME instance/request
ID and accepts a proven Applied receipt without executing the edit a
second time. A normal unknown-ID receipt from a healthy server still
supports automatic same-ID submission.

Test fixtures: `manualRetryAfterJournalRecovery` and
`ordinaryUnknownStillRetriesOriginal` in
`tests/physis_remote_edit_test.cpp`. These are AUTHORED; native CTest,
macOS/Windows validation and true journal+World crash-atomicity are
NOT VERIFIED. Handoff:
`docs/agent-handoffs/physis-stage44-manual-retry-2026-11-19.md`.

### Stage 45 — prevent lock-file FIFO startup hangs (POSIX)

For the canonical admin journal, `JournalProcessLock::acquire`
opens `<journal>.lock` with `O_RDWR|O_CREAT|O_NONBLOCK` (plus
`O_CLOEXEC` and `O_NOFOLLOW` where supported). This is required:
a malicious FIFO can block the server BEFORE its `fstat` regular-file
security check if the open is not nonblocking. Reject non-regular,
multiply hardlinked, wrong-owner or group/other-accessible inodes.

The PHYSIS CTest source `unsafeJournalLockInodesRejected` now verifies
a FIFO is refused, in addition to permission and hardlink checks.
An **isolated Linux GCC 14.2 C++20 header smoke** passed with exact
source blob SHA `835cfb9630e66d7918ed91ef06f97f590c2007b5`.
This is NOT a full engine CMake/CTest or platform validation. See
`docs/agent-handoffs/physis-stage45-fifo-lock-hardening-2026-11-19.md`.

### Stage 46 — trusted directory anchored journal lock

On POSIX, PHYSIS admin journal lock acquisition now opens the DIRECT
parent as a verified directory fd with `O_DIRECTORY|O_NOFOLLOW`
where supported, rejects directories not owned by the effective UID
or group/world writable, and resolves the lock basename using
`openat`. After `flock`, it checks that visible lock and parent
device/inode identities still match the opened descriptors; any
mismatch fails closed. Preserve Stage 45 nonblocking FIFO and
regular inode/single-link/no-shared-permissions guards.

These checks protect the **acquisition boundary**, not against
post-acquisition malicious same-user rename/unlink. Never grant
untrusted users write access to the journal parent; do not claim
distributed fencing or parity with Windows ACL/reparse protections.

Source fixture: `unsafeJournalLockParentsRejected` in
`tests/physis_admin_journal_test.cpp` on POSIX. The exact current
header blob `3fcd9f029bc7cc71f939c4471828acfbc358b843` passed an
isolated GCC 14.2 C++20 harness for owner/mode, symlink parent, FIFO,
lock modes and hardlinks, cross-process exclusion and SIGKILL release.
Full AIVERSE CMake/CTest/macOS/Windows tests remain UNVERIFIED.

Stage handoff:
`docs/agent-handoffs/physis-stage46-lock-parent-integrity-2026-11-19.md`.

### Stage 47 — live journal lock identity before all admin writes

A POSIX `JournalProcessLock::acquire()` only establishes the initial
`flock`. Before privileged admin ingress, each committed tick's
receipt reconciliation, and each FileJournal write, use
`JournalProcessLock::valid()` to recheck the visible lock's device
and inode, owner/mode/link count, and the original parent directory
identity and private permissions. `LoopbackAdminEndpoint` performs
the checks and invokes sticky `ServerGateway::suspendJournal()` when
ownership is lost. Do not directly bypass the endpoint's protected
journal-writer callback.

After a live lock rename/unlink, another process may successfully
flock a different inode at the old filename. The original endpoint
must fail closed and NOT advertise Queued/Applied receipt durability.
This is best-effort verification at admission/write boundaries, not
a cryptographic or distributed fencing token and NOT protection
against a same-user pathname race between verification and writing.

Regression fixtures `replacedLiveLockSuspendsAdmin` and
`liveParentPermissionChangeSuspendsAdmin` were added to
`tests/physis_admin_journal_test.cpp`. The exact current header
blob `040fedb623d491428057542eee2f8fae6bcc2a28` passed an
isolated GCC 14.2 Linux C++20 smoke including live rename/chmod
and Stage 46 checks. Full AIVERSE CMake/CTest, macOS and Windows
endpoint QA are NOT VERIFIED; Windows still requires handle/ACL
identity validation. See
`docs/agent-handoffs/physis-stage47-live-lock-integrity-2026-11-19.md`.

### Stage 48 — canonical pinned POSIX journal I/O

Before work, verify that `main` is already included in the current
PHYSIS branch; a GitHub compare with `main` reported behind=0 at
stage 48, so do not make an unnecessary merge commit.

On POSIX the canonical `LoopbackAdminEndpoint` now loads via
`FileJournal::loadPinned(JournalProcessLock::directoryFd(),gateway)`
and writes via `FileJournal::savePinned(dirfd,entries)`. The
directory fd is retained from lock acquisition. `openat/renameat`
keep temp creation and target replacement within the originally
verified directory even if an ancestor path is subsequently renamed;
`valid()` fails closed if published lock/parent identity is lost.
Existing `FileJournal::save/load/attach` are not automatically
lock-protected, and Windows still needs equivalent handle/ACL checks.

The new POSIX regression sources
`pinnedJournalSaveDoesNotFollowMovedParent` and
`pinnedJournalLoadRejectsUnsafeInodes` belong to
`tests/physis_admin_journal_test.cpp`. An isolated GCC C++20 smoke
verified the exact lock header SHA
`2512aefb07f612c808041b92a21da897df4f195b`, and a separate
native pinned-dirfd `openat/renameat` smoke passed. The full
FileJournal/AIVERSE CMake/CTest/macOS CI are **NOT VERIFIED**.
See `docs/agent-handoffs/physis-stage48-pinned-journal-io-2026-11-19.md`.

### Stage 49 — standalone tested POSIX receipt journal I/O

Use the canonical `FileJournal::loadPinned/savePinned` for
privileged POSIX admin receipt persistence; they now delegate raw
filesystem operations to `engine/physis/PhysisJournalPosixIO.h`
(`posix_journal::read/write`). The helper has NO engine dependencies
and accepts a trusted pinned directory fd from
`JournalProcessLock::directoryFd()`. Never use a client-provided
journal path or bypass lock ownership.

`read` fails closed on unsafe inode type, owner, mode, hardlinks,
oversize/short reads and symlink/FIFO tricks, while preserving the
caller buffer after failure. `write` creates an unpredictable
O_EXCL temporary file, flushes/atomically renames within the SAME
dirfd and syncs the directory. Receipt byte format and canonical
ECS authority are unchanged.

New `tests/physis_journal_posix_io_test.cpp` is a focused CTest
target. On Linux, the exact committed source blobs for that header
and test matched `git hash-object` and the isolated native test
PASSED on GCC 14.2 C++20 CTest (1/1), Clang 17 strict-warning run,
and GCC ASan/UBSan. This is a real **isolated low-level PASS**, not a
full AIVERSE/Ubuntu+macOS CI or admin endpoint PASS.
Full engine checkout/CTest remains unavailable due GitHub DNS and
queued/cancelled workflow runs. Continue testing once CI completes.

See `docs/agent-handoffs/physis-stage49-posix-journal-test-2026-11-19.md`.

### Stage 50 — verify journal identity after read and fail closed on collisions

The POSIX-only `PhysisJournalPosixIO.h` helper now verifies that
the opened journal fd, final published pathname (via `fstatat`
without symlink following) and the original directory still refer
to safe, consistent filesystem objects after reading. It rejects
replaced or resized files instead of exposing stale bytes to
`ServerGateway::restoreJournal`.

Writes still use `openat(O_EXCL)` and atomically replace via
`renameat`, but now also verify the parent directory remains
trusted after `fsync`. Refuse colliding temp names without deleting
or overwriting another file. Any failed write or read must remain
fail closed, with no false Applied/Queued durability ACK.

Source test `physis_journal_posix_io` covers these negative cases.
The exact committed source blobs
`fc1b07631c5191e8681992fb3743482029c7d2ea` (header) and
`f3500d80dc7c99c389b12e0d8a40721e202d8048` (CTest)
passed an isolated Linux CMake/CTest 1/1, strict GCC 14.2 and
Clang 17 C++20 builds, and GCC AddressSanitizer/UndefinedBehaviorSanitizer.
Full AIVERSE and native authenticated admin integration have NOT
been built or run; GitHub feature CI was queued/cancelled.

Handoff:
`docs/agent-handoffs/physis-stage50-post-read-integrity-2026-11-19.md`.

### Stage 51 — verify real pinned admin startup against unsafe receipts

The canonical privileged `LoopbackAdminEndpoint::open` must call
`FileJournal::loadPinned` before binding a network listener, and
must release the process lock on failure. The new POSIX fixture
`pinnedEndpointRejectsUnsafeStartupJournal` tests valid startup,
corrupted checksum refusal, released lock, symlink/FIFO refusal and
successful startup after the operator repairs the file. It is
authored in `tests/physis_admin_journal_test.cpp`, but the complete
native AIVERSE executable has NOT been compiled or run.

The low-level `posix_journal::read/write` helpers also reject
embedded `\\0` in journal basenames and temporary suffixes, so
POSIX `openat` cannot silently truncate a name. The source-identical
test for this change passed on Linux (isolated CTest 1/1, GCC, Clang,
ASan, UBSan), with code blobs
`3e00b8ec1508c438f401c240eaa3aa38ab90bbd1` and
`a2393a8d09a75429d9e291a537682c72dff09e3f`.

No journal schema, ECS component, native game genre, network
authority or NEREID/TerraWeave/Box3D ownership changed. See
`docs/agent-handoffs/physis-stage51-pinned-startup-2026-11-19.md`.

### Stage 52 — lock validation after startup load; post-rename fsync errors

The canonical `LoopbackAdminEndpoint::open` now checks the live
`JournalProcessLock::valid()` again after `loadPinned` and before
binding its privileged loopback listener. A path/lock mutation during
load must stop startup and release ownership, even if receipt decoding
succeeded. Reconcile the World checkpoint separately from the v1
receipt sidecar; do not report false Applied.

The stand-alone `physis_journal_posix_io` CTest now contains a
Linux `O_PATH` failure-injection case: the atomic rename succeeds
but `fsync(parentFd)` is unavailable, so the writer returns false.
A visible new file is NOT proof of crash durability. Do not
silently retry with a new request ID.

The exact updated test blob
`c9c7063481b15d9e3d96fc3c8b022e86a32f30ff` passed
isolated GCC 14.2 CMake/CTest 1/1, Clang 17 strict warnings, and
GCC ASan/UBSan on Linux. Full AIVERSE, admin server CTest,
macOS and Windows remain UNVERIFIED.
Handoff:
`docs/agent-handoffs/physis-stage52-startup-revalidation-fsync-2026-11-19.md`.

### Stage 53 — publish only the inode that was fsynced

For canonical POSIX journal writes, `posix_journal::write` now records
the staged temporary inode's `fstat` metadata before close. After
`renameat`, it uses `fstatat(AT_SYMLINK_NOFOLLOW)` to confirm that
the visible target still names this exact regular, single-linked,
server-owned, private and correctly sized inode. A mismatch fails
closed, as do fsync or parent permission failures; the endpoint
must not send a durable Applied/Queued receipt in that case.

The existing standalone `physis_journal_posix_io` test additionally
checks inode replacement, secure ownership/mode and temp cleanup.
Its exact committed header and test blobs
`2dd114731b498c4156da305915201f97c4848647` and
`7c6cd0ef9e330be5b61bf7935bb8d8b0f025a907`
passed Linux CMake/CTest 1/1, strict GCC/Clang and ASan+UBSan.
Full AIVERSE root build, macOS and authenticated admin tests remain
UNVERIFIED. This is not a distributed fence or cross-file transaction.

Handoff: `docs/agent-handoffs/physis-stage53-published-inode-2026-11-19.md`.


### Stage 54 — real journal lock + pinned POSIX I/O test

The isolated `physis_journal_locked_io` CTest compiles
`PhysisJournalProcessLock.h` and `PhysisJournalPosixIO.h`
together without ECS, net or editor dependencies. It verifies that
a valid `JournalProcessLock::directoryFd()` supports
`posix_journal::read/write`, that a competing owner fails, and
that renaming a live lock inode invalidates `valid()` and
`directoryFd()` even though a separate owner can now acquire
the replacement inode. Never use the low-level I/O helpers directly
from an authenticated admin endpoint after lock ownership is lost;
the canonical endpoint must fail closed before persistent writes.

The test is registered in CMake and the PHYSIS Linux/macOS workflow.
An isolated source-identical native Linux GCC C++20 CTest passed
1/1, with Clang 17 strict warnings and GCC ASan/UBSan also passing.
Source hashes: lock `2512aefb07f612c808041b92a21da897df4f195b`,
I/O `2dd114731b498c4156da305915201f97c4848647`,
test `db075dd14dd47f77a719fa47a86cce4adbac71ae`.
This is NOT a full engine CTest PASS; macOS and the integrated
authenticated `physis_admin_journal` CTest remain unverified.

Handoff:
`docs/agent-handoffs/physis-stage54-locked-io-test-2026-11-19.md`.


### Stage 55 — orphan temp is not committed journal history

The POSIX `FileJournal` loader reads only the canonical
`receipts` path. A crash before `renameat` can leave
`receipts.next-*` files; they must not be interpreted as committed
request/receipt records. New calls to `posix_journal::write` refuse
an empty payload before creating a temp file. Do not remove orphan
files by default, as they may be forensic evidence. The
`physis_journal_posix_io` regression checks these policies.
Actual isolated Linux CTest 2/2 and strict GCC/Clang/ASan/UBSan pass;
full AIVERSE `physis_admin_journal` remains unverified. Handoff:
`docs/agent-handoffs/physis-stage55-interrupted-temp-recovery-2026-11-19.md`.

### Stage 56 — bounded pinned journal I/O

Direct calls to `PhysisJournalPosixIO.h` are now bounded to 16 MiB,
matching the canonical `FileJournal` V1 persistence limit. A caller
cannot use a larger `maxBytes` read bound, reversed min/max bounds,
or an oversized/empty write, even if it bypasses serialization.
`PhysisRemoteJournal.h` statically asserts that the limits match.

The tests prove that invalid writes do not create temp files and
invalid reads do not modify output. Both isolated native Linux
journal CTests passed (2/2), as did strict GCC/Clang and ASan/UBSan.
This does NOT establish a full engine or cross-platform test PASS.

Handoff: `docs/agent-handoffs/physis-stage56-bounded-posix-journal-2026-11-19.md`.

### Stage 57 — standalone PHYSIS journal CI entry point

Run focused native journal tests without configuring the entire engine:

```sh
cmake -S tests/physis_journal_standalone -B build/physis-journal-standalone -DCMAKE_BUILD_TYPE=Debug
cmake --build build/physis-journal-standalone --parallel 2
ctest --test-dir build/physis-journal-standalone --output-on-failure
```

The standalone CMake compiles real lock and raw POSIX I/O production
headers from the repository, not mocks. The `physis.yml` Ubuntu/macOS
workflow includes a separate job for these tests while retaining
full `physis-native` checks. The exact CMake file and four source
blobs passed a local Linux GCC 14.2 CMake/CTest 2/2. GitHub Actions
runners were still queued; do NOT report Ubuntu/macOS CI as passed.
See `docs/agent-handoffs/physis-stage57-standalone-ci-2026-11-19.md`.

### Stage 58 — independent native journal CI workflow

Use `.github/workflows/physis-journal.yml` for the two standalone native journal tests on Ubuntu and macOS. It is an independent workflow with a separate concurrency group; a source or documentation push cancelling `physis.yml` must no longer cancel journal checks. The original `physis.yml` remains responsible for full PHYSIS engine, network, persistence, Box3D and CTest coverage.

Local standalone build and CTest completed with **2/2 PASS** under Linux GCC. The newly created GitHub workflow runs for journal and complete engine were QUEUED when last inspected. Do not equate a queued GitHub run with successful macOS/Ubuntu tests.

Stage handoff: `docs/agent-handoffs/physis-stage58-independent-ci-2026-11-19.md`.

### Stage 59 — process crash between temp fsync and rename

The physis_journal_crash_recovery POSIX CTest uses actual
PhysisJournalProcessLock and PhysisJournalPosixIO production
headers and a real fork/SIGKILL. It demonstrates that an abandoned
fsynced .next-uncommitted file after a crash is NOT recognized as
committed receipt history, and that the next owner can safely
reacquire the flock and continue canonical journal writes.
Use the deterministic pipe handshake, not arbitrary sleeps.

The standalone Linux native CTest passed 3/3, strict Clang 17 and
GCC ASan+UBSan passed. Full AIVERSE, macOS runners, authenticated
admin test and Windows are still UNVERIFIED.
See docs/agent-handoffs/physis-stage59-process-crash-recovery-2026-11-19.md.

### Stage 60 — avoid cancelling native CI with documentation-only changes

The full PHYSIS `.github/workflows/physis.yml` includes a NEGATIVE
path filter `!engine/physis/AGENTS.md` after the positive
`engine/physis/**` pattern in both push and PR triggers. Updating
this handoff policy file alone must no longer enqueue a new full
native matrix and cancel queued actual code checks. Continue writing
AGENTS.md and a dedicated docs/agent-handoffs/ file after EVERY
stage; the other code/test paths still trigger normal validation.

The independent journal workflow
`.github/workflows/physis-journal.yml` remains separate.
Current isolated Linux journal tests re-ran successfully (3/3),
strict Clang 17 all 3 PASS and GCC ASan/UBSan SIGKILL test PASS.
This is not full engine/macOS/Windows CI. Handoff:
`docs/agent-handoffs/physis-stage60-ci-doc-filter-2026-11-19.md`.


### Stage 61 — run full PHYSIS validation with retained logs

Use the new Bash 3.2-compatible
`bash tools/physis-validate-native.sh --full --jobs 4`
on a full macOS/Linux checkout of the PHYSIS branch. It configures a
focused headless Debug CMake build, builds the PHYSIS native target
list and runs `ctest -R '^physis_'`, preserving
`configure.log`, `build.log`, `ctest.log` and `summary.txt`
under `build/physis-validation-full/physis-reports/`.
Failures return nonzero and MUST be reported with actual logs.

For no-dependency filesystem testing use
`bash tools/physis-validate-native.sh --standalone --jobs 2`.
That exact script passed Linux native CTest 3/3 with GCC 14.2 after a
syntax check. FULL engine mode, macOS, and editor-core authoring
checks have NOT been run. The default full mode includes Box3D;
`--no-physics` excludes that optional target.
See `docs/agent-handoffs/physis-stage61-native-validation-runner-2026-11-19.md`.

### Stage 62 — focused admin/edit/journal validation

On a complete AIVERSE macOS/Linux checkout, use:
```sh
bash tools/physis-validate-native.sh --integration --no-physics --jobs 4
```
This configures the ROOT engine CMake but builds only
`physis_admin_journal`, `physis_remote_edit`,
`physis_field_runtime` and the three standalone POSIX journal
executables, then runs just those six CTests. Use `--full` for
the entire headless PHYSIS suite including Box3D, or
`--standalone` to avoid the engine dependency graph entirely.

Actual tests: the script passed `bash -n`; its integration branch
passed with six fake CMake targets and correctly propagated an
injected CTest failure (exit 8). This is **runner plumbing QA only**.
The genuine standalone journal CTest passed 3/3 on Linux. The
full-engine integration tests are still unverified. Preserve
configure/build/ctest report logs in the next handoff.

See `docs/agent-handoffs/physis-stage62-targeted-integration-runner-2026-11-19.md`.

### Stage 63 — reject revision probes when durable receipts are unavailable

A `RemoteClient::requestRevision()` sends a Query for a fresh
request ID. Its normal Unknown reply carries the authoritative
revision. However `Result::Unknown` with
`kJournalRetrySuspended` / `kJournalDurabilityUnavailable`
means the server cannot guarantee durable request deduplication.
The client MUST keep `hasRemoteRevision()==false`, set
`ClientStatus::Uncertain`, and show the error rather than
implicitly enabling remote editor submissions. After journal
repair and a fresh server endpoint, an explicit revision probe
may clear the failure and return the client to Idle. Never treat
the unhealthy Unknown as a safe baseline.

Regression source:
`tests/physis_remote_edit_test.cpp` (function `revisionProbeStopsOnUnhealthyJournal`).
This high-level integration fixture is authored but has NOT
executed on a full engine build. The unrelated standalone POSIX
journal CTests still pass 3/3. To validate the actual fix on a
native checkout, run:
```sh
bash tools/physis-validate-native.sh --integration --no-physics --jobs 4
```
Handoff:
`docs/agent-handoffs/physis-stage63-revision-probe-durability-2026-11-19.md`.

### Stage 64 — remote ACK monotonicity and Applied retry

A terminal PHYSIS remote receipt must not be rewound by delayed
Queued/Unknown/Denied/etc. A previously Applied request may ONLY be
verified again by a new Query after reconnect. Ignore received
pre-query frames and Queued during verification. An absent prior
Applied receipt is Uncertain and requires an explicit manual retry
with the SAME original ID; never silently re-Submit it.
When verification times out, `RemoteClient::retry()` issues Query
only for the already-Applied ID. A new `begin()` must reject
Uncertain and in-progress Applied verification. This is protocol V1;
there is no query nonce/generation, so absolute cross-reconnect
freshness needs further protocol design.

Regression source: three Stage 64 fixtures in
`tests/physis_remote_edit_test.cpp`. AUTHORED, NOT EXECUTED
in full native AIVERSE. Run
`bash tools/physis-validate-native.sh --integration --no-physics --jobs 4`
and preserve actual reports before changing PR #25 from Draft.
Handoff:
`docs/agent-handoffs/physis-stage64-monotonic-ack-2026-11-19.md`.

### Stage 65 — successful macOS integration after reconnect fixes

The real native macOS PHYSIS integration runner initially failed
`physis_remote_edit` and `physis_admin_journal` (4/6 PASS). Fix
`e628ba359a31be4e8fa2d16d8fab156e1e212e4d` corrected
send-side Applied status preservation on reconnect Query and a
backwards synthetic WS test clock. The user then reran the exact
`--integration --no-physics` target: **BUILD PASS, CTest 6/6
PASS, 0 failures, 2.99s**. Native report location:
`build/physis-validation-integration/physis-reports/20261119-102507-474`
(macOS path prefixed with user's AIVERSE checkout).

Do NOT report this as full-engine/Box3D/editor PASS. Next run:
```sh
bash tools/physis-validate-native.sh --full --jobs 4
```
Inspect failures, keep PR #25 Draft, update handoff after each stage.
Full detail in
`docs/agent-handoffs/physis-stage65-macos-integration-fixes-2026-11-19.md`.

### Stage 66 — deterministic gravity force test normalization

The macOS full PHYSIS runner at `3a1c1536` built successfully and
passed 23/24 native CTests. Only `physis_forces` failed because a
test assumed `Fixed::fromRatio(-981,100) * 10` was bit-identical
to `Fixed::fromRatio(-981,10)`. These Q47.16 rounding routes differ
by exactly two LSBs; changing production force physics is NOT warranted.
Commit `2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90` makes the
test assert the actual `mass * quantized gravity` policy exactly
and bounds physical error at two raw ticks.
Revised native test is AUTHORED, NOT RUN.
Run after pulling the PHYSIS branch:
```sh
bash tools/physis-validate-native.sh --full --jobs 4
```
Do not claim 24/24 without actual output. See
`docs/agent-handoffs/physis-stage66-fixed-gravity-rounding-2026-11-19.md`.

### Stage 67 — PHYSIS native full 24/24 PASS, editor-core verification next

User's native macOS `--full` run after Stage66: **24/24 CTest
PASS, 0 failed, 10.51 seconds**, including PHYSIS physics/Box3D,
storms, force sampling, journaling, admin and remote editing.
Report: `build/physis-validation-full/physis-reports/20261119-105206-10283`
(on user's AIVERSE checkout). Do NOT reclassify the two
editor-core MapDoc tests as covered: `physis_authoring` and
`physis_field_authoring` require `aiverse_editor_core`.

Use the real CMake target/test names and a separate directory:
```sh
cmake -S . -B build/physis-editor-validation -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF
cmake --build build/physis-editor-validation --target aiverse_physis_authoring_test aiverse_physis_field_authoring_test --parallel 4
ctest --test-dir build/physis-editor-validation --output-on-failure -R '^physis_(authoring|field_authoring)$'
```
If native compiler or CTest fails, report the first actual error.
Handoff: `docs/agent-handoffs/physis-stage67-headless-green-2026-11-19.md`.

### Stage 68 — Editor MapDoc authoring CTests are now verified on macOS

User-provided real native `build/physis-editor-validation` results:
`physis_authoring` PASS (3.28s) and `physis_field_authoring`
PASS (2.70s), 2/2, 0 failures, EDITOR/PHYSIS label total
5.99 sec*proc. This was a separate editor-core enabled CMake build,
not part of the previously confirmed 24/24 headless `--full` run.
Combined, the user has provided 26 PASS executions across two runs.
No code changes or additional local test execution in this stage.
The actual editor UI/mouse/Undo/Redo, full repo-wide CTest, and
cross-platform CI are still not verified. Retain PR #25 Draft.
See `docs/agent-handoffs/physis-stage68-editor-authoring-green-2026-11-19.md`.

### Stage 69 — C++ editor::net shadow guard for PHYSIS EditorLink

When compiling PHYSIS inside the actual AIVERSE editor, `net::` inside
`namespace aiverse::editor` resolves to `aiverse::editor::net`, NOT
`::aiverse::net`. The header `apps/editor/PhysisRemoteEdit.h` must
use explicit `::aiverse::net::` for transport types, crypto wipe and
connection states. The editor window must call
`::aiverse::net::nowMs()`. A fake editor::net namespace declared
before including the bridge in `tests/physis_remote_edit_test.cpp`
keeps this compile regression exercised in native PHYSIS CTest.
GUI compilation failed before this fix; after-fix native build/CTest
remains NOT RUN. Do not claim editor GUI success. See
`docs/agent-handoffs/physis-stage69-editor-net-shadow-2026-11-19.md`.

### Stage 70 — C++ ternary and ::aiverse::net qualifier syntax

Stage69's bulk replacement fixed `editor::net` shadowing but produced
`():::aiverse::net::Bytes{}` in a ternary expression.
The user-provided macOS compiler correctly rejected this at
`apps/editor/PhysisRemoteEdit.h:169`. Stage70 commit
`4817aaadebe20dd9f77e2d057d3ff275848e1416` uses:
`client_ ? client_->exportOutstanding() : ::aiverse::net::Bytes{}`.
Keep the colon as a separate ternary operator preceding absolute
namespace resolution. Source audit finds no other `:::` sequences
in the PHYSIS editor bridge/window/regression test. Real
post-fix compilation and CTest have NOT RUN. Next: pull PHYSIS
branch and compile/test `aiverse_physis_remote_edit_test`, then
`aiverse_editor` on macOS; keep PR #25 Draft until actual logs.
Handoff:
`docs/agent-handoffs/physis-stage70-ternary-net-qualification-2026-11-19.md`.

### Stage 71 — macOS AiverseEditor compiles after Stage69/70 fixes

The user provided **actual** `physis_remote_edit` CTest **1/1 PASS**
(0.08s) and **`[100%] Built target aiverse_editor`** on the
PHYSIS feature branch after `4817aaad`. This is native compiler
evidence that `::aiverse::net` type resolution and the ternary
separator are repaired. No C++ change in this stage.

The UI has not yet been launched/inspected. Open the freshly built
macOS bundle found beneath `build/physis-gui-validation`; PHYSIS
window is `Мир → PHYSIS · Природа` and closed by default.
Without a live GameModule PHYSIS installation, the standalone demo
is safe and opt-in; the Scene overlay requires the real ECS field.
Save observations/screenshots and then a new agent handoff. Do not
mark full GUI, cross-platform CI or production admin security PASS.
Handoff:
`docs/agent-handoffs/physis-stage71-macos-gui-build-green-2026-11-19.md`.

### Stage 72 — merge current main / NEREID into PHYSIS

The PHYSIS branch now contains main NEREID water via true merge
`7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b` with main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` as second parent. Do NOT
implement an independent water-body, buoyancy, river/current or water
renderer in PHYSIS. Use canonical
`<aiverselib/water/NereidWaterSystems.h>` and NEREID scene APIs
from the current main. PHYSIS is authoritative ONLY for weather,
wind, storms, gravity and environmental hazards. When the native
host installs NEREID water authority, set PHYSIS flood mode to
`FloodMode::ExternalAuthority`. The opt-in
`engine/world/NereidPhysisWindBridge.h` consumes host-sampled wind
for **visuals only**, not persistent water simulation.
No automatic dataflow/precipitation-to-volume conversion is shipped.
Static GitHub checks passed, actual combined native compiler/CTest
are NOT RUN. Prior separate 24/24 PHYSIS, 2/2 editor authoring,
NEREID 119/119 and GUI build are not a current-merge PASS.
Handoff:
`docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.

### Stage 73 — 40/40 combined PHYSIS/NEREID/SpectraFX CTest PASS

After main/NEREID was merged into `feature/physis-nature-simulation`,
the owner ran focused native macOS CTest and supplied **40/40 PASS,
0 failed**, 10.25 seconds. The scope is `^(physis_|ecs_nereid_|vfx_)`,
with 26 PHYSIS-labelled and 5 SpectraFX-labelled CTests; editor,
persistence, network, physics/security label groups overlap.
Stage72's "combined suite NOT RUN" is now superseded, but complete
repo-wide regression, same-World host integration and live Metal
rendering remain NOT VERIFIED. Do not infer automatic PHYSIS rain
to NEREID water levels or authoritative water changes via visual
wind bridge. Handoff:
`docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

### Stage 76 — inspect warnings/errors during builds

Use `python3 tools/aiverse-progress.py --mode build --build-dir
build/physis-nereid-validation --jobs 4` to see pinned colored
Warnings/Errors diagnostics while the CMake build continues.
Error entries remain visible even after later warnings. Consult
`<build-dir>/aiverse-reports/<stamp>/diagnostics.log` for
full paths and `summary.json` for per-stage counts.
Python monitor regression CTest is `tool_aiverse_progress`.
Source commit `47a320624a118d3f5dd23a7e9d4c81d58e095676`.
Standalone Linux fixture checks passed; actual macOS TTY view
and a full AIVERSE build with the new version remain NOT RUN.
Release Place runner (prior monitor) was user-reported PASS in
1:41; exact average tick not provided. See handoff:
`docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

### Stage 78 — sample PHYSIS environment into ECS contacts

Use `NatureProbeComponent` + `NatureContactComponent` only
for entities needing an authoritative environment snapshot.
Enable `InstallOptions::enableNatureContacts=true` explicitly
in `GameModule::setup`; PHYSIS samples once after its climate
pass via `physis.nature_contacts`, leaving movement, weather,
water-body updates and rendering to their owners. These components
and `io(Archive&, ...)` schema registration are optional to
preserve legacy saves.

For opt-in NEREID water contact on the SAME entity, host also
registers NEREID persistence and a water sampling system.
`engine/world/PhysisNereidNatureBridge.h` provides
`readNatureWaterContact(world,entity,view)`; it rejects stale
tick stamps or mismatched probe positions and does not mutate
either subsystem. Use PHYSIS `FloodMode::ExternalAuthority`
when NEREID is water authority.

New test `tests/physis_nereid_nature_contacts_test.cpp`,
CTest `physis_nereid_contacts`: authored, NOT EXECUTED; compile,
run and fix actual failures before production use. Handoff:
`docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.

### Stage 79 — reusable nature structural stress

`NatureStressReceiverComponent` describes resistance (shear
Newton limit, bending Newton-metres limit, lever arm, catastrophic
ratio, fatigue/recovery/fire/quake per-tick coefficients).
`NatureStressOutputComponent` publishes aerodynamic force,
bending load, normalized overload, persistent accumulated damage
and a latched `newlyFractured` transition. Use the independent
PHYSIS system `advanceNatureStress(World&)` through
`InstallOptions::enableNatureStress=true`, which requires
`enableForces` and `enableNatureContacts`.

The system must NEVER apply Box3D forces, move foliage, spawn a
falling tree, alter NEREID water or mutate TerraWeave terrain.
The host consumes the fracture transition at the authoritative
commit barrier, deduplicated by `(Entity, sampledClimateTick)`,
to enable its appropriate physical/foliage response. Grass
should be sampled as a cluster/batched stand-in, not per blade.
Only ECS data is persisted; neither wind particles nor graphics
state becomes deterministic gameplay state. A separate
opt-in same-world water bridge is in
`engine/world/PhysisNereidNatureBridge.h`.

Native tests `physis_nereid_contacts` and
`physis_nature_stress` are **WRITTEN but NOT RUN** on the
latest source. Handoff:
`docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

### Stage79 template regression note

When creating PHYSIS ECS components in fixture code,
`World::add<T>(entity, Args&&...)` is a variadic template;
do not pass an untyped `{}` as an argument. Use
`World::add<T>(entity, T{})` or `World::add<T>(entity)`.
The Stage79 test's 12 occurrences were fixed in commit
`146013f2e27cdefe24d6f224c6e30bad88c22251`.
This is an API source inspection, NOT native compile or CTest.

### Stage 80 — const World API in PHYSIS/NEREID tests

For `const World&`, use `tryGetConst<T>(Entity)` with a null check;
`World::get<T>(Entity)` is mutable-only. Both Stage78 same-World test
reads were fixed at `4d9c1adb94f88c7aefef58e3320afda9297e0b48`.
Owner then reported `AIVERSE: PASS` (20s, zero warnings/errors).
Inspect the actual report's `ctest.log` for per-test results.
Handoff: `docs/agent-handoffs/physis-stage80-const-world-native-2026-11-19.md`.

### Stage 81 — actual macOS NatureContacts/NatureStress CTests PASS

Owner supplied full per-test CTest evidence after the
`const World` fix, source `4d9c1adb94f88c7aefef58e3320afda9297e0b48`:
`physis_nereid_contacts` 0.85s PASS and
`physis_nature_stress` 1.01s PASS; **2/2, 0 failed**, full
build monitor PASS in 20.71s with 0 diagnostic warnings/errors.
This verifies the standalone same-World PHYSIS/NEREID contact
fixture and deterministic stress tests; NOT full game
TerraWeave/tree falling integration. See
`docs/agent-handoffs/physis-stage81-nature-tests-green-2026-11-19.md`.

### PHYSIS Stage 82 — host fracture outbox workflow

Enable `enableFractureOutbox` with `enableNatureStress`,
`enableForces` and `enableNatureContacts`. Place
`NatureFractureOutboxComponent{}` on an entity before fracture.
After a committed tick, call `pendingNatureFractures(world)`
and durably stage the host-owned fallen-tree/Box3D/vegetation
reaction; only then
`acknowledgeNatureFracture(world,entity,eventKey,climateTick)`.
No ack on failed host action. Pending persists over later ticks
and native saves; rolling back before ack re-exposes pending.
TerraWeave already owns PlantFallEvent/FallenPlantPhysics on a
separate branch; do not create another implementation in PHYSIS.
Source `2b0190cb93e4335e7e458fa03d75239ef65c8424`.
CTest `physis_fracture_outbox` authored, **NOT RUN**.
Handoff:
`docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`.
