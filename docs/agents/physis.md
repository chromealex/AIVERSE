# Agent rules: PHYSIS environmental ECS: diagnostics, stages 15-79, map authoring

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## PHYSIS — genre-independent environmental ECS (2026-11-18)

- **Canonical source:** `engine/physis/{Physis,PhysisClimate,PhysisEnvironment,PhysisDisasters,PhysisWind,PhysisGravity,PhysisStorm,PhysisForces,PhysisPresentation,PhysisIntegration}.h`. Native games include these headers and install PHYSIS into generic `aiverse::Simulation` through `physis::install`; do **not** reimplement weather/hazards in FPS, RTS, RPG, any genre, or in renderer-specific code.
- **Mandatory independence:** PHYSIS must have no dependency on any game-genre module or a render backend; use `World` ECS components/resources and `Fixed` arithmetic, fixed committed ticks, deterministic seeds and COW/rollback.
- **Presentation:** consuming Terra, Water, Foliage, VFX, Physics, Audio, editor and native gameplay may query read-only `EnvironmentalResponse` / `PresentationCell`; PHYSIS must work headless. Do not store visual handles in authoritative ECS.
- **System ordering:** one of `advance`, `advanceClimate`, `advanceEnvironment` or `advanceEnvironmentWithDisasters` per authoritative tick; do not advance all of them. `physis::install` registers a single system. Preserve world persistence schemas when changing components.
- **Docs/hand-off on EVERY stage:** `docs/agent-handoffs/physis-{core,climate,environment,disasters,integration}-2026-11-18.md` and `engine/physis/AGENTS.md`. Every follow-on implementation stage must commit code, then a separate handoff with exact branch/SHA, public API, changed files, actual test results, limits and specific next action.
- **Tests:** baseline CTests `physis_core`, `physis_climate`, `physis_environment`, `physis_disasters`, `physis_integration`, plus `physis_wind`, `physis_gravity`, `physis_storm`, `physis_forces`, `physis_presentation`, and opt-in `physis_box3d` (`AIVERSE_ENABLE_PHYSICS=ON`); run `./run-tests.sh --filter '^physis_'` as required by the unified test-runner policy above. These targets have been written and registered but **NOT RUN** in the connector-only development session; do not claim validation or merge before tests and review.
- **Nature presentation host (2026-11-21):** `engine/world/PhysisNature{Presentation,Cues,FxHost,AudioHost,Grass}.h` drive TerraWeave grass burn/wind, SpectraFX fire/smoke/light and audio from read-only PHYSIS; editor «Живая природа» (`apps/editor/PhysisNaturePreview.h`, MCP `physis_nature_preview`) shows it without a play-test. Reuse these; do not write a second fire/grass/VFX bridge. Game client hookup and macOS visual QA pending: `docs/agent-handoffs/physis-nature-host-2026-11-21.md`.
- **Automatic + parallel + streaming (2026-11-22):** a map `"physis"` block installs PHYSIS in `Simulation::setup` for every module (`engine/physis/PhysisAutoInstall.cpp`, CMake `AIVERSE_SIM_PHYSIS`); `engine/world/PhysisNatureRuntime.h` drives SpectraFX/audio/TerraWeave wind/NEREID wind/PRISMA sky from it in the game client — reuse it in other hosts, do not wire hosts by hand. Grid passes take `workerThreads` lanes (`PhysisParallel.h`, bit-identical; per-cell writes only, providers read-only) and `streaming.chunkCells` + `ActivityAnchor` (`PhysisStreaming.h`). CI builds the aggregate target `aiverse_physis_tests`; never hand-list PHYSIS test targets. Handoff `docs/agent-handoffs/physis-next-2026-11-22.md`.
- **Tornadoes (2026-11-22):** map `"physis.tornadoes"` → `engine/physis/PhysisTornado.h` (`TornadoTrack` on a Tornado `StormEvent`: wander/waypoint paths, life ramp, fire blown out/fanned, debris resource, `NatureCarriable` props) + system `physis.tornadoes`; unit/building damage only through the generic `GameModule::environmentDamage` (RTS override in `RtsCombat.cpp`). Presentation (funnel/debris FX, roar, vegetation/water wind) in `PhysisNatureRuntime` via `PhysisTornadoCues.h`. Tests `physis_tornado`, `physis_tornado_cues`; docs/physis.md "Tornadoes"; handoff `docs/agent-handoffs/physis-tornado-2026-11-22.md`.
- **Fire whirls, water, ground scour (2026-11-22):** `engine/physis/PhysisTornadoNature.h` (`TornadoNature` on every map tornado: drawn fire -> fireness, ignition/embers/heat damage; land tornado weakens over water, waterspouts; `GroundScourField`). Water under a vortex only via `GameModule::environmentWaterDepth` (or PHYSIS local flood). Map keys: tornado `fire`/`surface`/`erosion`, `physis.ignitions`/`disasterConfig`/`noFuelOnWater`/`scourCellMeters`. Tests `physis_firestorm`; handoff `docs/agent-handoffs/physis-firestorm-2026-11-22.md`.
- **Remaining work:** PHYSIS-owned climate/wind/gravity/storm/force fields and host-facing presentation APIs and host-owned opt-in Terra/NEREID/VFX/Foliage/Physics/editor adapters (other agents own canonical water and terrain), spatial indexing, sparse hazards, streaming/LOD, deterministic partitions, network deltas, platform validation and physical-unit calibration. Follow the cross-agent ownership matrix below and `docs/physis-nereid-terraweave-integration.md`; do not duplicate Water or Terra systems.

## PHYSIS diagnostics / visualization boundary (2026-11-18)

- PHYSIS Stage 14 adds a read-only, bounded CPU diagnostic raster in `engine/physis/PhysisDiagnostics.h`, with `sampleDiagnosticRaster`, `DiagnosticLayer` and 16-bit `diagnosticNormalized`. This is reusable by any editor/game/server but is not an implemented Scene overlay or VFX. The API must not write ECS state, inspect render handles or mutate Box3D/Water/Terra.
- World state remains owned by PHYSIS fixed-point ECS; diagnostics sample only committed `World` state, with invalid/out-of-grid cells explicitly marked. `LocalFloodMeters` is PHYSIS's standalone flood, never canonical NEREID water.
- The added `physis_diagnostics` CTest is registered for `./run-tests.sh` and focused `.github/workflows/physis.yml`; it is **NOT RUN** until actual CI/native results are inspected. Handoff `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`.

## PHYSIS Stage 15–16 — diagnostic fast path and snapshot broadphase (2026-11-18)

- `engine/physis/PhysisDiagnostics.h` reads scalar weather/surface/hazard layers **directly** from authoritative ECS cells. Only wind/gravity vector layers need a 3D field query. Old `sampleDiagnosticRaster` signatures and raw-output semantics remain.
- `engine/physis/PhysisSpatialIndex.h` is an **opt-in transient COW-snapshot index**, not persistent ECS state: X/Z buckets prune WindZone, WindShelter, GravityField and StormEvent candidates while keeping exact old Fixed evaluation policies, ECS visit order and priority tie-breaks. `sampleDiagnosticRasterIndexed` is available to editor/host consumers.
- Rebuild the derived index after committed simulation ticks, storm movement, mutations, load or rollback when current values are required. Never store it in `World` resources, serialize it, make rendering authoritative, or depend on Box3D/FPS/RTS/Water/Terra in PHYSIS core.
- Stage tests `physis_diagnostics` (14-layer old-vs-fast and indexed parity) and `physis_spatial_index` (negative positions, field parity, snapshot mutation, bounded global fallback) are registered for the unified CTest and focused Linux/macOS workflow. **NOT RUN**; no benchmark throughput or native success claim. Handoffs: `docs/agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md`, `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`.

## PHYSIS Stage 17 — editor diagnostics and separate preview World (2026-11-18)

- The PHYSIS agent owns `engine/physis/PhysisDebugPalette.h` (CPU-only colour conversion, not authoritative) and `apps/editor/windows/PhysisWindow.cpp` (new optional docked GUI, auto-registered by `AIVERSE_EDITOR_WINDOW`). This **does not** replace or refactor shared Inspector/Scene/Hierarchy from editor-unification PR #7. The window opens from the World menu and is hidden by default.
- The PHYSIS window may **read** the current `Editor::sim()->world()` if its `physis::State` exists, but must never step or modify that World or Box3D. When there is no PHYSIS live simulator, an explicit action creates an **isolated demo World** and any step buttons apply only to it.
- 2D heatmaps use `DiagnosticRaster`, `FieldSpatialIndex`, and `DebugPalette`. Wind/Storm/Gravity circles are visualization only, not colliders or physical objects. Do not duplicate NEREID water or Terra terrain. Avoid introducing FPS/RTS-specific debug paths.
- Tests `physis_editor_view` plus prior PHYSIS regression targets are registered with unified CTest and focused CI, but **NOT RUN**. Also build `aiverse_editor` and manually inspect Russian labels, cell picking, project/source switching and UI layout before claiming editor usability. Stage handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## PHYSIS Stage 18 / current main merge — 2026-11-18

- **Canonical feature branch** `feature/physis-nature-simulation` merged `main` commit `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` via two-parent merge `810feba70f01cdc35003ac80ea8486f39dc16049`, preserving input work and PHYSIS/SpectraFX/VFX. `docs/agent-handoffs/physis-main-merge-2026-11-18.md` records the exact trees/conflict resolution. Native merged build/CTest have NOT RUN.
- **Scene debug overlay:** `apps/editor/PhysisSceneOverlay.h` draws opt-in transparent PHYSIS field tiles and Wind/Storm/Gravity rings in the *real* Scene viewport; `engine/physis/PhysisSceneGeometry.h` is pure Fixed-point CPU geometry, independent of editor/Box3D/genres. The Scene projection is presentation-only and never edits authoritative World.
- **Inspector authoring:** `apps/editor/PhysisAuthoring.h` validates map-level `"physis"` JSON and uses one `MapDoc::beginEdit()` per Apply for Undo/Redo. `apps/editor/windows/InspectorWindow.cpp` shows PHYSIS parameters under `Selection::MapProps` plus extended JSON; supports both generic map and FPS document layouts. Invalid edits must have no side effects.
- **Runtime opt-in:** `engine/physis/PhysisProjectConfig.h` parses map config into `InstallOptions`; any generic native `GameModule::setup` may call `physis::installFromMap(sim,map)`. Do not auto-attach to RTS/FPS genre modules or mutate live ECS when authoring. Authored per-volume arrays are stored but not auto-spawned until a separate stable-ID adapter exists.
- **Verification gates:** `physis_scene_geometry` and `physis_project_config` build under base tests, `physis_authoring` builds when `aiverse_editor_core` is enabled, Studio workflow compiles/executes the editor integration tests. Actual CMake/CTest, editor screenshots and live UI tests: **NOT RUN**. Handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`. Preserve PHYSIS/NEREID/TerraWeave ownership, main branch input changes, and editor-unification PR #7 work.

## PHYSIS Stage 19 — authored nature fields, universal ECS spawn and editor gizmos (2026-11-18)

- **Genre-independent source of truth:** `engine/physis/PhysisFieldAssets.h` validates map-level `windZones`, `storms`, `gravityFields` with stable unique authored IDs and creates ordinary `WindZone`, `StormEvent`, `GravityField` components in sorted ID order. `engine/physis/PhysisProjectConfig.h::installFromMap` runs all validation before world initialization; explicit native `GameModule::setup` remains required. Never duplicate these calculations in FPS/RTS/RPG modules.
- **Editor owner:** `apps/editor/PhysisFieldEditor.h` + `PhysisAuthoring.h` list, create, edit, remove fields and provide opt-in W/S/G Scene markers. Click/drag is handled before normal selection ONLY when marker editing is enabled and the pointer hits a marker. No fake runtime ECS selection, live World edits or Box3D force changes. Selected authored field properties are shared with the universal MapProps Inspector.
- **Undo/redo rule:** field addition/deletion/property Apply uses `MapDoc::beginEdit` once. Scene drag starts a single snapshot on the first effective movement and calls `MapDoc::touched` on subsequent moves. All authoring, including FPS map JSON, must use the same strict parser and the same MapDoc history. Do not mutate game save ECS directly; there is no live hot-reload in this stage.
- **IDs/saves:** Stable ASCII `id` (1..64 chars) is a map authoring key, sorted at spawn; it is not itself a new persistent ECS schema. Nonempty field collections require matching `enableWindZones`, `enableStorms`, `enableGravityFields` flags. Use unchanged map + registered schema manifest for replay/load; no runtime field-ID lookup or multi-user edit merge claimed.
- **Water/Terra:** NEREID Water agent retains authoritative water; `FloodMode::ExternalAuthority` prevents duplicate PHYSIS shallow-water authority. TerraWeave agent owns foliage/landscape. Box3D adapter remains optional physics consumer.
- **Tests:** `physis_field_assets`, `physis_field_authoring` (when editor_core built), expanded `physis_project_config` native save fixture, CMake + Linux/macOS/Studio CI updates. **CMake, native C++, CTest and graphical drag testing NOT RUN**; don't claim success without logs. Next handoff `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`.


### PHYSIS Stage 21 — group editing without runtime writes
- PHYSIS Scene W/S/G markers now support Ctrl/Shift multi-selection,
  group move, safe duplicate and atomic group deletion. Continue building
  genre-independent ECS components/systems; avoid genre-only variants.
- The shared authoring helpers moveFields/duplicateFields/removeFields use
  native field validation for one MapDoc Undo transaction per batch/drag,
  for both regular and FPS-verbatim JSON maps. Move a whole group by the
  same delta; never snap members individually or mutate live ECS World.
- Keep Scene/ImGui in apps/editor/PhysisFieldEditor.h. NEREID owns water,
  TerraWeave terrain/foliage, Box3D dynamics. No new saved ECS schema.
- See docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.
  Source test cases added but C++ compilation/CTest/macOS GUI QA NOT RUN.
  PR #25 must stay draft until verified.


- PHYSIS Stage 21 extension: physis_authoring::transformFields performs one
  editor-only group rotation around global Y and uniform spatial scale
  about the active selected marker. It scales radii/storm extents but NOT
  wind velocity or force strengths. No new runtime authority or schema.
  See the Stage 21 handoff; GUI/CTest verification still pending.


### PHYSIS Stage 22 — advanced Scene field gizmos

- PHYSIS mouse controls in apps/editor/PhysisFieldEditor.h now include
  optional box selection, 5 pivot choices, world-axis X/Y/Z handles,
  Yaw-Y and Uniform Scale rings, and prior radius editor. Pure
  apps/editor/PhysisGizmoGeometry.h is ImGui/genre independent.
- Pivots are transient editor state. Group movement snaps the pivot and
  preserves relative offsets; ring drag calculates from original JSON
  via transformFields with baseline/firstEdit and a single Undo.
  Every result gets full native parser validation before MapDoc write.
- Never edit live ECS/Box3D/NEREID/TerraWeave state from scene authoring.
  Yaw changes field positions, not orientation or wind velocity. The
  current field types do not support unconstrained 3D quaternion rotation.
- Independent engine/physis/PhysisFieldChangePlan.h may exist from
  another agent; this is a planning helper only, not ECS hot reload.
- Stage handoff docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.
  C++ fixtures authored but native compile/CTest/macOS visual QA NOT RUN;
  keep PHYSIS PR #25 in draft.


### PHYSIS Stage 23 — opt-in reconciler and command authority

- The new engine/physis/PhysisFieldRuntime.h provides generic,
  genre-neutral stable authored ID -> Entity mapping and an opt-in
  Simulation command-boundary reconciler based on FieldChangePlan.
  installEditableFromMap intentionally changes persistent schema
  (physis.field_runtime v1, Wind/Storm/Gravity pools); keep old
  installFromMap and existing save manifests untouched by default.
- Apply complete typed field edits through a host-selected
  GameModule::applyCommand type after server authorization and revision
  checking; never write live ECS from Scene. Stable IDs and fingerprints
  live in rollback-owned World resource; same-kind Update preserves Entity,
  retype Remove+Add, stale batches reject and no-op batches consume revision.
- Simulation Command wire supports <=4096 args, so PHYSIS caps snapshots
  at 32760 bytes. It does not provide automatic network permissions,
  atomic multi-packet chunking, Inspector-to-GameModule dispatch or
  GUI edit acknowledgement. Live climate/grid/flood policy migration
  remains unsupported.
- A new physis_field_runtime CTest target is registered in CMake and
  PHYSIS/Studio CI. Test SOURCE has been written but native C++/CTest/CI
  PASS and actual network/editor integration have not been verified.
- Stage handoff: docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.
  Keep PR #25 as DRAFT until genuine native validation.


### PHYSIS Stage 24 — opt-in editor submission and network authority

- apps/editor/PhysisLiveEdit.h holds the generic local capability and
  status tracker, apps/editor/windows/PhysisWindow.cpp the UI.
  GameModule must opt in, select its own command type and authorize
  LOCAL editor mutations explicitly. No PHYSIS World writes from Scene.
- Queued is not Applied. Read runtime revision plus authored field
  fingerprints, invalidate on superseding changes/rollback and visibly
  reject stale revisions or missing dispatch.
- engine/physis/PhysisFieldRuntime.h exposes server-side
  admitAuthorizedFieldEdit with a principal generated from the
  authenticated connection and a host ACL callback. Never promote
  untrusted player=0 commands into authority. Existing player input
  relay is not a PHYSIS admin endpoint.
- Network CommandSink stays unavailable for local editor submit; a
  remote host needs its own privileged transport, routing and ACK/NACK.
  No automatic FPS/RTS integration, climate/water/physics hot edits or
  default persistence schema changes.
- New physis_live_edit CTest + CI entries are authored but native
  compile/CTest/macOS UI/real network runs NOT VERIFIED.
  Handoff docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


### PHYSIS Stage 25 — remote editor protocol, dedupe and reconnect

- Reuse net::Conn and versioned PHYSIS envelopes for remote editing;
  do not inject editing into normal player InputFrame or allow
  arbitrary client-issued player=0/system commands.
- ServerGateway takes a verified principal from an authenticated
  server connection and a host ACL; only server-owned commands
  execute at Simulation boundaries. Deduplicate exact requests
  by principal/instance/request ID, reject altered ID reuse,
  serialize admissions and distinguish Queued from Applied.
- Applied requires the right ECS revision and exact fingerprints,
  not a sent packet. RemoteClient preserves ID and bytes on retry,
  queries before resending after reconnect and rechecks old
  Applied receipts for rollback. Server dedupe cache is volatile,
  NOT an exactly-once journal across server process restarts.
- apps/editor/PhysisRemoteEdit.h accepts only a trusted attached
  net::Conn. No default anonymous PHYSIS admin listener or
  FPS/RTS auto-authority. A host must wire real auth, transport
  and durable journal separately.
- Another agent's RuntimeFieldRegistry / installTrackedFromMap is
  independent from the Stage 23 FieldRuntimeState pipeline.
  Keep one authority, do not install both. No NEREID/Terra/Box3D
  ownership changes.
- CTest physis_remote_edit and PHYSIS/Studio workflow targets
  added but native compile/CTest/real security QA NOT RUN.
  Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


### PHYSIS Stage 26 — journal recovery and privileged local endpoint

- Canonical PHYSIS editor/runtime authority is Stage 23/24
  FieldRuntimeState + installEditableFromMap + GameModule::applyCommand
  dispatching applyFieldEditCommand. Do NOT install the parallel
  RuntimeFieldRegistry/FieldCommandHost simultaneously.
- engine/physis/PhysisRemoteJournal.h stores the admin request/receipt
  cache as bounded checksummed write-ahead snapshots. Persist INTENT
  before command enqueue and status after tick, fail closed if disk
  writes fail. On startup restore Simulation checkpoint first,
  validate/load journal second, open privileged listener third.
- engine/physis/PhysisAdminAuth.h provides challenge/proof with keyed
  BLAKE2b and secure OS random. PhysisAdminEndpoint.h binds a
  WsListener to loopback only, requires game-owned reserved command
  type, authenticated principal and ACL. Never trust client IDs
  or player=0 as an authority credential.
- Native net::connectWebSocket does not support WSS/TLS. External
  admin requires a real encrypted tunnel/transport, never public
  plaintext ws://. No automatic privileged listener is added to
  FPS/RTS modules or ordinary net match input.
- Sidecar file journal + separate World checkpoint is NOT one
  crash-atomic transaction; Unknown/Conflict after ambiguous
  restart is a correct result, not a false Applied. Further
  atomic checkpoint work required before exactly-once guarantees.
- Test target physis_admin_journal registered and fixture written;
  native build/CTest/macOS editor/actual remote TLS and security QA
  NOT RUN. Stage handoff
  docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### PHYSIS Stage 26 hardening — queued receipt after process crash

- ServerGateway::restoreJournal must convert every persisted Queued
  receipt to Unknown: a server restart loses its old in-memory
  Simulation.pending commands. First restore matching World,
  then observe() to reconcile to Applied / Conflict from actual
  revision and field fingerprints. For unresolved intent permit
  only the identical original authenticated Submit under ACL
  and expectedRevision. Never fake a successful old receipt.
- New native source tests in physis_admin_journal cover crash
  before scheduled tick and crash after World commit but before
  receipt flush; BUILD/CTEST NOT RUN.
- FileJournal sidecar and Simulation save are NOT one atomic store.
  Keep exact limits/ownership and separate PHYSIS, NEREID,
  TerraWeave and Box3D. Handoff:
  docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### PHYSIS Stage 26 queued receipt crash recovery
- The existing admin journal and authenticated loopback endpoint were already implemented in PHYSIS; do not duplicate them. `ServerGateway::restoreJournal` must treat persisted `Queued` as `Unknown` on process restart because the Simulation pending command may have vanished.
- Reconcile Unknown against the restored World; confirm Applied only for matching revision and typed-field fingerprints. Safe requeue requires the exact original authenticated request and still-current expected revision. Do not claim cross-file exactly-once durability.
- New source tests cover crashes before ECS commit and after ECS commit but before journal Applied flush. Native tests NOT RUN.
- Stage handoff: `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`.

## PHYSIS synchronized with canonical NEREID Water in main (2026-11-19)

The two-parent merge `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b` imported `main` at `d71c6d47e532b25fb816a8bb1f96c41e5275a165` INTO
`feature/physis-nature-simulation`; **never merge PHYSIS into main
without separate review**. The merge preserved the genre-independent
PHYSIS ECS and imported canonical NEREID Water (components, surface,
buoyancy, currents, confluence, scene, renderer bridges and tests).
NEREID owns authoritative water bodies/flows/buoyancy; PHYSIS owns
weather, wind, gravity, storms, wetness/snow/ice and hazards. For a
native host using NEREID water authority, explicitly opt PHYSIS into
`FloodMode::ExternalAuthority` to avoid double shallow-flood simulation.
`engine/world/NereidPhysisWindBridge.h` is presentation-only; it never
makes PHYSIS and NEREID share mutable authoritative state.
Follow `docs/physis-nereid-terraweave-integration.md` and
`docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.
Previous PHYSIS/Box3D, Editor authoring and NEREID standalone test
results do NOT establish a PASS for the new combined merge: run a
new native cross-module compile/CTest and preserve the result.

## PHYSIS + NEREID post-merge native test checkpoint (2026-11-19)

User ran the PHYSIS branch's focused post-`main` merge CTest
(`^(physis_|ecs_nereid_|vfx_)`) and reported **40/40 PASS,
0 failures, 10.25s**. This verifies those native test targets after
the two-parent merge `7c617d7f`; it is **not** all-repo CTest or
runtime visual validation. The system authority contract is unchanged:
PHYSIS owns environment/weather; NEREID owns water bodies/flow
and buoyancy; TerraWeave owns terrain/foliage. No duplicate local
PHYSIS shallow flood with canonical NEREID: choose
`FloodMode::ExternalAuthority`. The existing PHYSIS-to-NEREID
wind adapter changes visuals only, not ECS water authority.
See `docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

## PHYSIS full repository test follow-up: skill audit and Place performance (2026-11-19)

After the native post-main-merge focused suite passed 40/40,
the user's full 150-test macOS CTest had **148/150 PASS**.
`skill_audit` failed on one PHYSIS documentation reference
(`tests/physis_remote_edit_test.cpp::revisionProbeStopsOnUnhealthyJournal`
was treated as a nonexistent file), while `place` had **51/52
internal checks PASS** but missed a strict performance check:
server tick average **8.204 ms**, required **<5.0 ms**.
All Place convergence/correctness assertions reported in the log
passed; the measured sample had 523 physics bodies, 16 clients/4g
and a late joiner.

Source commit `a61923df653994a9a94c1916f4077db7fb7e9848`:
- In `skills/world/aiverse-physis/SKILL.md` separate the valid
  file path from the function name so `tools/skills/audit.py`
  no longer treats the combined `file.cpp::function` as a path.
- In `CMakeLists.txt`, make `place` CTest `RUN_SERIAL TRUE`.
  This schedules the wall-clock performance test exclusively,
  eliminating competition with parallel CTests as a confound.
  The **original <5.0ms** performance condition and full scenario
  are unchanged; no Place simulation/physics runtime was modified.

**Verification:** CMake 3.31 isolated fixture confirmed
`RUN_SERIAL=True` in `ctest --show-only=json-v1`.
GitHub source readback confirmed the updated skill reference.
**NOT RUN after these changes:** the actual macOS `skill_audit`,
`place`, complete 150-test suite. This is a scheduling mitigation,
not proof that Place meets 5ms on the user's machine. If it still
fails alone, capture Release/Debug build mode, CPU/Rosetta status
and isolated tick timing before changing performance budgets.
Handoff: `docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`.

## PHYSIS Stage 78 — optional per-entity Nature Contacts (2026-11-19)

New `engine/physis/PhysisNatureSampling.h` adds reusable
`NatureProbeComponent` (3D Fixed position/enabled),
`NatureContactComponent` (wind, gravity, atmospheric and surface
fields, fire/quake, validation and two tick stamps), and
`advanceNatureContacts(World&,uint32_t)`.
`InstallOptions::enableNatureContacts` defaults to **false**;
the optional components are persisted only when enabled and
`physis.nature_contacts` runs AFTER the climate system. No
historical save manifest or genre-dependent code is changed by default.

Separate `engine/world/PhysisNereidNatureBridge.h` combines
the published PHYSIS/NEREID read-only contacts on one ECS entity,
checking enabled state, matching tick stamps and probe positions
to avoid stale data. It neither advances water nor writes
NEREID bodies. Host must register both samplers; choose
`FloodMode::ExternalAuthority` if NEREID owns water.

**Code commits:** `354e0689fffcfc4ec9853307fc3a06ba32c86212`,
`a286022cb733c524f30b795501c957ee1ae3f9ec`.
**Verification:** source and test registered in CMake; combined native
`physis_nereid_contacts` test is WRITTEN, **NOT RUN** on macOS
or other full AIVERSE checkout. The earlier 40/40 focused
PHYSIS/NEREID/SpectraFX PASS predates these commits and must NOT
be cited as testing Nature Contacts. Review possible compile or
runtime errors in the new test rather than claiming readiness.

Handoff: `docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.

## PHYSIS Stage 79 — generic wind/fire/quake structural stress (2026-11-19)

PHYSIS now publishes opt-in, deterministic environmental structural
stress **without** duplicating the existing wind force solver. Use
`engine/physis/PhysisNatureStress.h`:
`NatureStressReceiverComponent` for resistance, lever arm, fatigue,
recovery, fire/quake sensitivity, and
`NatureStressOutputComponent` for aerodynamic force, bending moment,
normalized load, accumulated damage and the latched fracture transition.
`InstallOptions::enableNatureStress=true` requires both
`enableForces=true` and `enableNatureContacts=true`. Systems run
climate -> forces -> nature contacts -> nature stress. Source:
`21c016ede31107f48c50ecc5247cfc0cf98ae56a`.

PHYSIS calculates generic **damage potential** only. It never
creates Box3D bodies, uproots trees, rewrites TerraWeave vegetation,
transforms meshes or runs a duplicate water authority. The host
reacts to `newlyFractured` once per (entity, climate tick); the
host must implement its own idempotent transition to fallen
vegetation/rigid bodies. For large grass fields use ECS **cluster**
proxies rather than a component per blade. A dedicated CTest
`physis_nature_stress` and the earlier
`physis_nereid_contacts` test are authored but **NOT RUN** natively.
No new feature may be declared validated until actual macOS CTest.
See `docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

### PHYSIS Stage 80 — native macOS recovery after const World fix (2026-11-19)

- Initial user-native AppleClang compile FAILED in `tests/physis_nereid_nature_contacts_test.cpp:103–104` with two `no matching member function for call to 'get'` errors. Test bound `const auto& world=sim->world()`, while `World::get<T>(Entity)` in `engine/core/ECS.h` is non-const.
- Implementation commit `4d9c1adb94f88c7aefef58e3320afda9297e0b48`: replace two reads with `world.tryGetConst<T>(entity)`, check both pointers, copy their component values. Existing mutable test setup continues using `get<T>`. No runtime ECS, PHYSIS, NEREID, Box3D, save schema or renderer changes.
- **Post-fix user-reported macOS native monitor: `AIVERSE: PASS`, 00:00:20, 0 warnings, 0 errors**. Report: `build/physis-nereid-validation/aiverse-reports/20261119-181448-67558`. The pasted output contains no CTest per-test lines: exact number of tests completed must be retrieved from this report's `ctest.log`/`summary.json`.
- Prior combined PHYSIS/NEREID/SpectraFX 40/40 was on older source. Full current-branch repository-wide tests, interactive editor, Box3D/TerraWeave fracture handoff and CI are not verified. PR #25 stays Draft.
- Standalone handoff: `docs/agent-handoffs/physis-stage80-const-world-native-2026-11-19.md`.

### Stage 81 — Stage78/79 confirmed native macOS CTest 2/2 PASS (2026-11-19)

The user supplied the complete AIVERSE Build Monitor JSON and native
`ctest.log` from `build/physis-nereid-validation`,
report `20261119-181448-67558`, on source commit
`4d9c1adb94f88c7aefef58e3320afda9297e0b48`:
`configure` exit=0, `build` exit=0, `ctest` exit=0,
**completed=2, failures=0**, aggregate exit=0, 0 warnings,
0 diagnostic errors, 20.71 seconds for the full command.
Native CTest:
- `physis_nereid_contacts` (#123) PASS 0.85s.
- `physis_nature_stress` (#124) PASS 1.01s.
- 100% PASS 2/2, 0 failed; CTest wall time 1.02s.
This supersedes Stage80's note that the named test results were
not yet available. **It does not establish full AIVERSE suite or
TerraWeave/Box3D physical falling integration.**
No code change; documentation checkpoint only.
Handoff: `docs/agent-handoffs/physis-stage81-nature-tests-green-2026-11-19.md`.

### PHYSIS Stage 82 — persistent fracture event handoff (2026-11-19)

Implementation commit `2b0190cb93e4335e7e458fa03d75239ef65c8424`.
Added `engine/physis/PhysisFractureOutbox.h` defining
`NatureFractureOutboxComponent` with Empty/Pending/Acknowledged
states, a generation-safe Entity/tick event key and persistent
Archive serialization. `advanceNatureFractureOutbox(World&)`
publishes a current-tick newlyFractured stress event once;
`pendingNatureFractures(const World&)` returns sorted pending
messages, retained over later ticks/save, and
`acknowledgeNatureFracture(World&,Entity,key,tick)` rejects
wrong/duplicate acknowledgments.

`InstallOptions::enableFractureOutbox=false` by default.
If enabled, it requires nature stress (already requiring forces
and nature contacts), registers `physis.fracture_outbox` v1
persistence and adds system `physis.fracture_outbox` after
`physis.nature_stress`. Default game save manifests unchanged.
`tests/physis_fracture_outbox_test.cpp` registered as CTest
`physis_fracture_outbox` (PHYSIS/ECS/PERSISTENCE labels):
tests ordering, deterministic keys, pending/ack, native
save/load, rollback and COW.

TerraWeave `feature/terraweave` already owns
`PlantFallEvent`, `AnchoredPlantId` and
`FallenPlantPhysics::spawn` for Box3D. PHYSIS does not
import or duplicate TerraWeave: a game host must map Entity
to plant ID, perform/replicate physical consequences and
then acknowledge. External Box3D float state is not atomic
with ECS ack; host must deduplicate/resynchronize on rollback.
NEREID still owns water. Grass uses per-cluster probes.

Prior Stage81 user macOS 2/2 nature tests PASS confirmed;
Stage82 native compile/CTest **NOT RUN**. PR #25 remains
Draft. Handoff:
`docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`.


## PHYSIS agent map authoring

Use `skills/world/aiverse-physis-authoring/SKILL.md` for `physis_get`,
`physis_validate`, `physis_set` in the shared editor/MCP Workspace. These
are revision-checked document edits with Undo/Redo, never live ECS writes.
For physical sky and local fog/reveal use the atmosphere reference linked
from `skills/visuals/aiverse-prisma/SKILL.md`; rendering remains PRISMA-owned.

## Hierarchical wind masks (2026-11-21)

Object sway = shared mask library + per-class profiles + per-prototype part
hierarchy: `engine/render/WindRig{,Shader,Model}.h`, Arbor glue
`engine/render/TerraWeaveArborWind.h`, `<model>.wind.json` sidecars applied by
`render::loadModel`. Reuse `windRigGraph()`; do not add per-object wind
shaders or new sway-weight vertex conventions. Skill
`skills/visuals/aiverse-wind-masks/SKILL.md`; handoff
`docs/agent-handoffs/wind-masks-2026-11-21.md` (Metal NOT verified).
