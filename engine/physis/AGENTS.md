# PHYSIS — required agent instructions

Scope: `engine/physis/`, `tests/physis_*.cpp`, `docs/agent-handoffs/physis-*.md`.

1. **Genre independence is non-negotiable.** PHYSIS engine files must not include anything from `modules/fps`, `modules/rts`, `libraries/include/aiverselib/genre`, or any game-specific types. Native projects use the common C++ API and ECS components, not forks per genre.
2. Long-lived state lives in `aiverse::World` components/resources, authoritative math uses `Fixed`. Renderer/FX/audio may consume snapshots and emit presentation events but must not own climate state.
3. Preserve deterministic ordering, fixed step scheduling, seed/replay, rollback/COW. No wall clock, platform distributions or float/double in simulation.
4. Integrate Terra, Water, Foliage, Physics, VFX via clean interfaces and opt-in adapters. No hard link to a rendering backend or platform.
5. After **EVERY** implementation stage write an explicit handoff in `docs/agent-handoffs/` with branch, implementation SHA, changed paths, architecture/API, tests ACTUALLY RUN and their results, known gaps, and next concrete task; commit it separately from implementation. Never mark unexecuted tests as passed.
6. Existing gameplay/genre modules must remain unaffected unless their integration is explicitly tested and reviewed. Respect concurrent branches; never force push.

Latest Stage 13: `docs/agent-handoffs/physis-sampling-persistence-2026-11-18.md`; Stage 12: `docs/agent-handoffs/physis-numerical-ci-2026-11-18.md`. Stage 11: `docs/agent-handoffs/physis-forces-box3d-2026-11-18.md`. Stages 8–10: `docs/agent-handoffs/physis-{wind,gravity,storm}-2026-11-18.md`; Stage 7: `docs/agent-handoffs/physis-flood-authority-2026-11-18.md`. Stage 6 role/ownership contract: `docs/agent-handoffs/physis-ownership-terra-water-2026-11-18.md`. Stages 1–5 remain preserved; no stage claims passed native tests.

## PHYSIS / NEREID / TerraWeave coordination (2026-11-18)

Read `docs/physis-nereid-terraweave-integration.md` (root `AGENTS.md` ownership matrix) before editing flood/water/terrain integrations.
- PHYSIS agent owns weather, dynamic precipitation, surface wetness/snow/ice, fire/quake and independent nature ECS. NEREID Water agent (PR #23, `feature/nereid-water-core`) exclusively owns canonical lake/river/ocean ECS water, water-body geometry, flow, water sampling, buoyancy and water surface pipeline. TerraWeave agent (`feature/terraweave`) owns terrain heights/biomes/foliage and later consumes both through **optional** adapters; PHYSIS PR #25 is `feature/physis-nature-simulation`.
- The local `HazardCell::waterDepth` shallow flood is **experimental standalone gameplay simulation**. For NEREID-managed water bodies, opt in to `FloodMode::ExternalAuthority` via `InstallOptions::floodMode`: local depth is cleared and flood calculations bypassed; PHYSIS fire/quake still run. This switch and tests are authored but native verification is NOT RUN. Never invent competing water sources or integrate normalized PHYSIS rain intensity directly into NEREID metres.
- PHYSIS core must NOT include NEREID or TerraWeave headers; TerraWeave core must not depend on PHYSIS/NEREID. Host-level integration owns conversions: Terra signed centimetres -> common signed Fixed metres, X/Z horizontal and Y vertical; floor division for negative world positions, elevation and shoreline coherence, explicit scheduler and persistence ownership.
- PHYSIS read-only `EnvironmentalResponse` is the input for Terra terrain material, wetness/snow masks and foliage wind; NEREID `WaterSample`/water bodies are the source for Terra waterline, exclusion/shoreline and buoyancy. No presentation feedback into authoritative state.
- On each subsequent implementation stage: code + unit/cross-system tests in a code commit, **separate** handoff documentation commit, scope/owner coordination and verified native build status. Do not edit the Water agent's or Terra agent's active branches and do not merge their PRs automatically.

## Stage 14 — diagnostics ownership (2026-11-18)

- `PhysisPresentation.h` and `PhysisDiagnostics.h` are **read-only**, renderer-independent consumer APIs. They return copies/arrays for external editor/VFX/physics/Terra host code. Do not add ImGui/RHI/Box3D/genre includes here, or treat a PHYSIS heatmap as a visual renderer integration.
- `DiagnosticRaster::rawValues` are `Fixed::raw` and `valid[i]` marks active sampled cells. Do not infer real NEREID water levels from `LocalFloodMeters`. `diagnosticNormalized` is presentation-only; it must not influence authoritative simulation.
- Add post-stage handoffs; Stage 14 `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`. The tests and CI are authored but native status is **NOT RUN**.

## Stage 15–16 spatial field reuse (2026-11-18)

- `PhysisDiagnostics.h` now samples basic scalar environment/hazard layers without full per-cell 3D field evaluation. New `PhysisSpatialIndex.h` builds a transient `World` COW snapshot, 2D candidate bins, and global fallback for huge volumes/uniform gravity; `sampleDiagnosticRasterIndexed` uses the same source snapshot.
- Do not change fixed-point math, Earth/Box3D gravity authority, and wind/storm candidate order in the unindexed reference. Existing `windAt`, `gravityAt`, `stormsAt`, and `sampleNatureField` remain supported. The indexed API invokes their **shared policy** through ordered visitors to make parity testable.
- The index is a derived read-only **snapshot**, not a mutable ECS mirror; rebuild for a new authoritative tick or after edits/rollback/load. It does not accelerate WeatherFront/Quake simulation, terrain/water, or external physics. Avoid retained historical snapshots.
- Handoff Stage 15: `docs/agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md`; Stage 16: `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`. All tests added, native compile/CTest/CI **NOT RUN** in connector environment.

## Stage 17 — editor debug view integration

- `engine/physis/PhysisDebugPalette.h` supplies only read-only display colour helpers. The editor PHYSIS window is `apps/editor/windows/PhysisWindow.cpp`, using the existing window plugin registrar; **do not** put ImGui or editor includes into `engine/physis`.
- Editor live PHYSIS is strictly read-only. Demo simulation is a fresh private `aiverse::World`, stepped only with explicit controls; never use it as a surrogate for actual game integration. The 2D heatmap outlines storm/wind/gravity volumes but is not a 3D Scene overlay or editable Inspector.
- Stage 17 test `physis_editor_view` / CMake / CI: authored, **NOT RUN**. Full editor build and macOS visual QA still mandatory. Handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## Stage 18 — Scene/Inspector and generic map config handoff

- `PhysisSceneGeometry.h` outputs reusable Fixed-position diagnostics only. The graphical projection and controls live in `apps/editor/PhysisSceneOverlay.h`, `apps/editor/windows/PhysisWindow.cpp`, and the shared `InspectorWindow.cpp`. Keep all ImGui/RHI/Box3D/editor includes OUT of PHYSIS core.
- `PhysisProjectConfig.h` may parse a map-level `physis` JSON block and call `installFromMap` explicitly from any native `GameModule::setup`. No per-genre automatic registration or direct renderer state. Inspector authoring uses `MapDoc` snapshots and validations and must not mutate live `Simulation::world()`.
- `physis_scene_geometry`, `physis_project_config`, and (when `aiverse_editor_core` exists) `physis_authoring` CTests are authored and registered, **NOT RUN**. The Scene GUI still requires native compilation and macOS visual/click tests before calling it functional.
- Merge prerequisite from main: `810feba70f01cdc35003ac80ea8486f39dc16049`, handoff `docs/agent-handoffs/physis-main-merge-2026-11-18.md`. Stage 18 handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`. Continue to document every implementation stage in a separate commit with real test results.

## Stage 19 — field asset importer and editor owned gizmos

`PhysisFieldAssets.h` is the canonical typed parser of authored `windZones`, `storms`, `gravityFields`. Validate stable IDs and fixed-point bounds before `installFromMap` modifies World; sort IDs across types before allocating entities. Keep it independent from all genres, editors, renderer, NEREID, TerraWeave, Box3D. Opt-in `enableWindZones`, `enableStorms`, `enableGravityFields` must match nonempty collections to preserve persistent component schemas.

The editor `apps/editor/PhysisFieldEditor.h` owns map-authored markers, click/drag and selected properties in shared MapProps Inspector, using `PhysisAuthoring.h` (same native field parser for validation). One drag opens exactly one MapDoc Undo snapshot; never write directly into running `Simulation::world()` from Inspector/Gizmo. Authored IDs are document keys, not replicated/persisted ECS IDs.

Tests `physis_field_assets`, `physis_field_authoring` and extended `physis_project_config` have source + CTest + CI registration, **NOT RUN**. Stage handoff: `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`. Run real editor/Box3D/PHYSIS tests before PR #25 is ready.

## Stage 20 — resize and snapping authoring invariants
- The editor offers opt-in Move/Radius gizmos and metre-grid snap; only `apps/editor/PhysisFieldEditor.h` uses ImGui, camera projection and mouse input. Keep `engine/physis` renderer/editor independent.
- `PhysisAuthoring.h::resizeField` edits outer storm radius or wind/gravity radius after the shared native parser validates the complete map. Move/resize drags call `MapDoc::beginEdit` only for the first effective edit, `MapDoc::touched` thereafter; preserve one Undo per drag. Never mutate the live World.
- Snap is a transient editor-only preference. Do not serialize it into PHYSIS ECS components or map assets. Existing map/save schemas and authored IDs remain unchanged.
- Handoff: `docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md`. New test fixtures were authored, **NOT RUN**; build/CTest/macOS Scene QA still required.


## Stage 21 — multi-field authoring guardrails
- apps/editor/PhysisAuthoring.h moveFields, duplicateFields and removeFields
  are editor MapDoc operations, never a live ECS write. Batch validate all
  field types before ONE Undo transaction. IDs belong to the authored map;
  they are not persisted ECS entity IDs.
- apps/editor/PhysisFieldEditor.h uses transient sorted multi-selection and
  a frozen drag-origin snapshot; one delta is applied to all fields and
  an anchor-grid snap must preserve relative offsets. Radius changes the
  active field alone. Switching Shift during group drag must not reset
  already moved axes.
- Duplication preserves unknown JSON properties while generating unique
  ASCII IDs under the 64-character limit. Enforce native parser shape,
  coordinates, 2048-field cap and per-category persistence enable flags.
- Preserve the genre-independent PHYSIS/NEREID/TerraWeave/Box3D boundaries.
  Source tests are authored but NOT RUN. Stage handoff:
  docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.


### Stage 21 grouped yaw/scale extension
- Use editor-only physis_authoring::transformFields for atomic group Y-axis
  yaw and uniform geometry scale around the active member, one Undo.
- Scale zone radius (and storm eye/outer radius/height), group-relative
  positions in XYZ, NOT wind velocity, storm strength or gravity forces.
  Validate all outputs against canonical parser BEFORE writing MapDoc.
- C++ fixture testGroupRotationAndScale has been authored but NOT RUN.
  No physics/runtime edit, new ECS component or save schema was introduced.


## Stage 22 — PHYSIS editor ownership and gizmo safeguards

- Pure scene math: apps/editor/PhysisGizmoGeometry.h. Mouse and ImGui:
  apps/editor/PhysisFieldEditor.h. Do not include either in engine/physis.
- Five pivot modes use physis_authoring::computeFieldPivot with selected
  authored IDs. Pivots and selection are session-only, never ECS schemas.
- World-axis X/Y/Z translation relies on RTSCamera::screenRay and exact
  ray-axis closest point; skip near-parallel rays. Marquee captures only
  opt-in empty-scene drag, not normal scene selection by default.
- Rotation ring Y changes field centre POSITIONS, not wind/gravity force
  vectors. Uniform scale adjusts source radii and storm heights as well as
  spacing, NOT intensities. Do not claim full 3D quaternion orientation.
- Dragging rotation/scale must pass initial JSON baseline to transformFields
  with firstEdit=false after the initial edit, preserving one MapDoc Undo.
  Validate the entire new authored config before writing; cancel on outside
  doc revision changes. Never write live PHYSIS World from Scene.
- A separate agent added pure PhysisFieldChangePlan.h. It is NOT an
  authoritative ECS hot-reload implementation; keep concerns separate.
- Stage handoff: docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.
  Source tests authored, native C++/CTest/macOS GUI NOT RUN.


## Stage 23 — opt-in live ECS field reconciliation

- New engine/physis/PhysisFieldRuntime.h is the ONLY generic runtime
  reconciler for stable authored source IDs; it composes the pure
  PhysisFieldChangePlan.h and existing PhysisFieldAssets.h parser.
- installEditableFromMap must be selected explicitly during GameModule
  setup. It installs all Wind/Storm/Gravity component save schemas
  and physis.field_runtime v1 resource. The normal installFromMap
  retains its legacy save manifest. Do not enable new schemas by default
  or claim automatic migration of existing saves.
- FieldRuntimeState lives entirely inside World; IDs map to generation-
  checked Entity handles, kind and canonical binary authored fingerprints.
  World snapshot/save/restore must include this resource. Updating a field
  of the SAME kind preserves the Entity, and unchanged storm sources retain
  their runtime drift. Retypes are Remove+Add, not in-place type swaps.
- A trusted GameModule must reserve a command type, authorize player=0
  on its network ingress and call applyFieldEditCommand ONLY inside
  Simulation::applyCommand at a committed tick boundary. Payload uses
  existing Command.args and is limited to 4096 64-bit words (32760 bytes
  of payload). A queued command is not an applied/acknowledged edit.
- Stale expected revision rejects a batch. All full desired typed fields
  must validate before any World mutation; stage an independent World copy,
  apply plan, then commit. Never mutate World directly from Scene or
  skip authoritative parser-range checks for network payloads.
- Field-only map submission rejects changed climate grid parameters.
  Flood/disaster policy and other PHYSIS settings are not live-migrated,
  nor are NEREID water/TerraWeave terrain/Box3D physics authorities.
- CTest physis_field_runtime is authored and registered, but native
  compile/CTest/editor integration/network service QA NOT RUN.
  Handoff docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.


## Stage 24 — authority and confirmation guardrails

- Stage 24 adds the non-ImGui apps/editor/PhysisLiveEdit.h local route and
  apps/editor/windows/PhysisWindow.cpp UI. A GameModule must opt into the
  FieldEditHost interface, explicitly allow trusted LOCAL editing, use
  installEditableFromMap and dispatch its own command type at the tick.
- Queued is only submission. Read World FieldRuntimeState revision and
  authored fingerprints to verify Applied; report conflicts, missing
  handler, rollback and superseded state instead of inventing ACKs.
- A network CommandSink makes the local editor route unavailable.
  For remote use, an authenticated server must call
  physis::admitAuthorizedFieldEdit with a trusted connection principal
  and its own ACL callback. The helper rejects untrusted player=0,
  validates revision and reencodes a new system command for submission.
  Never trust packet player or a client-provided permission boolean.
- No public admin transport, remote ACK, default genre integration,
  live climate migration, water/Terra/Box3D edit rights or new default
  ECS save schemas were added. A dedicated server endpoint must be
  integrated by a native project before claiming remote live editing.
- CTest physis_live_edit has source fixtures and CI registration,
  but real native compile, CTest, macOS GUI and remote network QA
  have NOT RUN. Stage handoff:
  docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


## Stage 25 — remote delivery and acknowledgement invariants

- PHYSIS remote admin protocol belongs to PhysisRemoteProtocol.h,
  PhysisRemoteClient.h and PhysisRemoteServer.h, over a dedicated
  authenticated net::Conn, NEVER the normal player InputFrame.
- ServerGateway accepts a PRINCIPAL from the host-authenticated
  connection. Require ACL callbacks. Command.player and client instance
  IDs are NOT authentication; trusted player=0 Commands are created
  by the server after admission.
- Cache receipt deduplication by (authenticated principal, client
  instance, request ID) with byte-exact payload comparison. Reuse
  with different payload rejects. Serialize new admissions until
  the previous PHYSIS edit resolves; Queued does NOT mean Applied.
- Applied requires scheduled Simulation tick completion and authoritative
  FieldRuntimeState revision plus typed fingerprints. On reconnect
  Query-before-resend, preserve request IDs and recheck Applied against
  rollback. Server cache is volatile; expectedRevision prevents
  duplicate effects but cannot guarantee old ACK after server restart.
- EditorLink under apps/editor/PhysisRemoteEdit.h is opt-in; the host
  attaches an ALREADY AUTHENTICATED net::Conn. No anonymous default
  PHYSIS endpoint, no FPS/RTS automatic admin permissions.
- Another agent's RuntimeFieldRegistry/TrackedFromMap is a separate
  opt-in store, NOT interchangeable with Stage 23 FieldRuntimeState.
  Do not enable both without explicit migration.
- No changes to NEREID water, TerraWeave, Box3D or default PHYSIS
  save schema. CTest physis_remote_edit authored but NOT RUN.
  Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


## Stage 26 — durable receipts and admin-only network authority

- Prefer canonical installEditableFromMap/FieldRuntimeState and
  applyFieldEditCommand for the editor and remote admin pipeline.
  The parallel RuntimeFieldRegistry / FieldCommandHost from other
  agents is a different opt-in authority. Never install both.
- Stage 26 adds PhysisRemoteJournal.h. The host MUST reload a matching
  Simulation checkpoint BEFORE FileJournal::attach, and attach the
  journal BEFORE any privileged connection can submit requests.
  Journal save errors FAIL CLOSED before admission; unknown original
  intents are persisted prior to enqueue. Never silently ignore a
  corrupted journal or overwrite it from another process.
- LoopbackAdminEndpoint (PhysisAdminEndpoint.h) binds strictly to
  loopback WsListener, requires a nonzero reserved PHYSIS command
  type, and authenticates a session via keyed BLAKE2b proof with
  fresh OS entropy. Server key lookup and ACL are host-owned.
  Never use Command.player, request ID, session token or user-provided
  roles to authorize. Do not alter regular player InputFrame.
- Native WebSocket currently lacks WSS/TLS. Do NOT expose ordinary
  ws:// admin service on a public interface. External administration
  requires a separate encrypted tunnel or actual native TLS.
- ServerGateway persists exact ID/bytes, typed fingerprints, Queued/
  Applied/NACK status and re-checks ECS after restart/rollback. The
  separate file journal and Simulation save are NOT an atomic
  transaction; Unknown/Conflict on ambiguous crash recovery is
  intentional. No crash-durable exactly-once promise until a shared
  checkpoint/journal transaction exists.
- Stage 26 test source and CTest physis_admin_journal are registered
  in CMake and PHYSIS/Studio workflows, but C++ compilation/CTest,
  real GUI/network/TLS QA are NOT RUN. Handoff:
  docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### Stage 26 correction — restore queued sidecar entries as unresolved

- ServerGateway::restoreJournal normalizes persisted Queued to
  Unknown and clears old scheduledTick. Never infer that a pending
  Simulation command survived a server-process restart.
- After restoring the corresponding World checkpoint, call
  observe() to verify revision+fingerprints. If not applied and
  revision still matches, ONLY a byte-identical original Submit
  from the same authenticated principal may re-admit through ACL.
  Keep request instance/ID and dedupe cache stable.
- The existing FileJournal and Simulation save are separate files:
  this recovery is NOT a crash-atomic transaction; ambiguous
  results remain Unknown/Conflict. Do not promise durable
  exactly-once ACK without coordinated checkpoints.
- Tests queuedReceiptBeforeCrash and
  queuedReceiptAlreadyAppliedBeforeCrash were added to
  tests/physis_admin_journal_test.cpp, but NOT RUN.
- Handoff docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### Stage 26 hardening — queued receipt recovery after server restart
- When restoring the Stage 26 durable receipt journal, `ServerGateway::restoreJournal` converts any pre-crash `Queued` entry into unresolved `Unknown`: a persisted queue receipt does not prove the transient Simulation command survived process restart.
- Call `observe` on the restored authoritative World before serving admin requests. Confirm `Applied` only if `FieldRuntimeState` expected revision and all authored fingerprints match; otherwise retain `Unknown` or return `Conflict`.
- Only the identical authenticated original Submit with matching expected revision may requeue an unresolved intent. Do not promise cross-file crash atomicity between journal and World snapshot.
- Tests `queuedReceiptBeforeCrash` and `queuedReceiptAlreadyAppliedBeforeCrash` are authored in `tests/physis_admin_journal_test.cpp` but native compilation and CTest have NOT RUN.
- Handoff: `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`.

### Stage 40 — real process lifetime and recovery testing (2026-11-19)

- `LoopbackAdminEndpoint` MUST retain its `JournalProcessLock`
  for the whole listener/peers lifetime. The lock member is declared
  before the listener, so C++ reverse-order destruction releases it
  only after the listener and gateway are destroyed. Never unlink the
  `<journal>.lock` inode while any endpoint is alive.
- POSIX regression tests in `physis_admin_journal` now fork a second OS
  process, verify nonblocking exclusion, kill the owner with SIGKILL,
  and check that a new process can acquire the same path. They also
  restore persisted Queued as Unknown after a killed child endpoint,
  then re-admit the exact authorized original and confirm durable
  Applied after the authoritative ECS tick.
- Windows has the existing same-process lock test, but no equivalent
  child-process SIGKILL fixture. Locking remains advisory and does not
  provide cross-host distributed fencing or joint World/journal commit.
- An isolated Linux GCC 14.2 C++20 header smoke was compiled with
  -Wall -Wextra -Werror -pedantic and PASSED the process-kill/reacquire
  scenario. This is NOT a full AIVERSE CMake/CTest PASS; native repository
  build, macOS/Windows QA and the new integrated test are NOT RUN.
- Stage handoff: `docs/agent-handoffs/physis-stage40-cross-process-journal-2026-11-19.md`.

### Stage 41 — CI queue control and post-crash lock cleanup (2026-11-19)

- In the `killedEndpointRecoversQueuedJournal` fixture, destroy the final
  `LoopbackAdminEndpoint` before removing the temporary journal directory.
  Deleting an active lock inode can silently defeat advisory protection:
  never call `remove_all` on a live journal+lock directory.
- `.github/workflows/physis.yml` now uses branch/ref-scoped concurrency
  with `cancel-in-progress: true` to cancel superseded PHYSIS runs. C++ build
  output is retained as `physis-build.log` through the existing artifact
  upload even when the build fails. `set -o pipefail` preserves failure status.
- At inspection time, 149 PHYSIS branch runs were queued, none had passed.
  New CI runs for both macOS and Ubuntu were still queued. Do not claim
  compilation, CTest or platform QA PASS until verified from job logs.
- Handoff: `docs/agent-handoffs/physis-stage41-ci-validation-2026-11-19.md`.

### Stage 42 — do not acknowledge unpersisted admin receipts (2026-11-19)

- When `ServerGateway::persistJournal` fails (including in
  `observe()` after a committed ECS tick or after command scheduling),
  `journalHealthy_` becomes false. A queued or applied receipt held
  only in process memory must be externally returned as
  `Result::Unknown`, never a confirmed `Queued` or `Applied`.
- New edit submissions fail closed. Unknown request IDs while the
  journal is unhealthy must NOT be called safe to retry; the server
  cannot guarantee deduplication until journal recovery/restart.
- In-memory ECS `FieldRuntimeState` remains authoritative for current
  simulation state, but the sidecar is not a joint transaction with
  World checkpoints. Preserve this uncertainty in remote UI.
- `tests/physis_admin_journal_test.cpp` contains writer-injection
  regressions for failed Queued/Applied persistence and denied new
  edits. Source authored, native CMake/CTest NOT RUN at this stage.
- Handoff: `docs/agent-handoffs/physis-stage42-durable-receipts-2026-11-19.md`.

### Stage 43 — suspend client retry when journal is unavailable (2026-11-19)

- PHYSIS V1 Receipt binary layout remains unchanged. Protocol constants
  `kJournalDurabilityUnavailable` and `kJournalRetrySuspended`
  identify `Result::Unknown` responses that MUST NOT trigger automatic
  `RemoteClient` retransmission. These strings are a V1 compatibility
  convention, not a new wire enum or a terminal applied result.
- `RemoteClient::poll` now transitions such responses to
  `ClientStatus::Uncertain` and stops automatic `Submit/Query` retries.
  Preserve the original pending request for explicit user retry only
  after the host's durable journal has recovered.
- Ordinary `Result::Unknown` without a journal-unavailable diagnostic
  continues the same-ID idempotent recovery path. Never change this into
  a new request ID. Do not treat Uncertain as success.
- New `unhealthyJournalStopsAutomaticResubmit` source fixture is in
  `tests/physis_remote_edit_test.cpp`; it checks that no application
  Submit/Query packets are emitted after a journal-unavailable response.
- Code and tests committed, but native C++ build, CTest and macOS/Windows
  runtime QA are NOT RUN. PHYSIS Actions had queued/cancelled jobs, no
  verified PASS. Handoff:
  `docs/agent-handoffs/physis-stage43-client-journal-backoff-2026-11-19.md`.

### Stage 44 — sticky journal uncertainty, reconnect and explicit retry (2026-11-19)

- `RemoteClient` MUST keep `ClientStatus::Uncertain` caused by a
  journal-unavailable Unknown across `disconnected()` and reconnect.
  Automatic Submit/Query is forbidden until explicit `retry()`.
- A delayed pre-failure Queued/Applied receipt must not clear this
  suspension. `suspendedForJournal_` is reset only when starting or
  restoring a distinct request or on explicit user retry; it is not
  an ECS-authoritative acknowledgment.
- Explicit `retry()` retains the original instance/request ID and
  probes the server first, so a recovered gateway can confirm Applied
  from journal + authoritative World without reapplying the edit.
  Ordinary Unknown without a journal-unavailable diagnostic retains
  existing idempotent same-ID recovery.
- New source fixtures `manualRetryAfterJournalRecovery` and
  `ordinaryUnknownStillRetriesOriginal` are in
  `tests/physis_remote_edit_test.cpp`; the former also injects a
  late Queued receipt after Uncertain and verifies it is ignored.
- Full native CMake/CTest, platform CI and editor UI execution are
  **NOT VERIFIED**; GitHub Actions remains queued/cancelled as inspected.
  Handoff: `docs/agent-handoffs/physis-stage44-manual-retry-2026-11-19.md`.

### Stage 45 — never block on a malicious POSIX journal lock file (2026-11-19)

- `JournalProcessLock::acquire` MUST open the POSIX lock path with
  `O_NONBLOCK` before `fstat` and `flock`. A user-controlled FIFO at
  `<journal>.lock` could otherwise block privileged admin startup before
  the inode-type guard had a chance to reject it.
- Continue requiring a regular single-link, current effective UID-owned
  lock file without group/other permission bits. The lock and its parent
  must be in a trusted server-owned directory. This is advisory
  single-host locking; it is NOT distributed fencing.
- `unsafeJournalLockInodesRejected` in `physis_admin_journal` now
  creates a malicious FIFO and asserts immediate refusal.
- Isolated Linux GCC 14.2 C++20 harness: PASS. It compiled the EXACT
  fetched PHYSIS header with Git blob SHA
  `835cfb9630e66d7918ed91ef06f97f590c2007b5`, and checked
  FIFO refusal, mode/hardlink denial, inter-process exclusion and
  SIGKILL lock release. Full AIVERSE CMake/CTest was NOT RUN.
- Handoff: `docs/agent-handoffs/physis-stage45-fifo-lock-hardening-2026-11-19.md`.

### Stage 46 — trusted POSIX journal lock parent and inode identity (2026-11-19)

- `JournalProcessLock::acquire` now opens the direct journal parent
  with `O_NOFOLLOW` and `O_DIRECTORY` where supported, validates that
  the parent is an actual directory owned by the server's effective
  UID and is not group/world writable (`mode & 0022 == 0`), and then
  resolves `<journal>.lock` via `openat(parentFd,...)`.
- After acquiring `flock`, validate that the visible lock path
  still resolves to the same device/inode as the locked fd and that
  the parent path still resolves to the originally opened directory.
  Release and fail closed if a replacement is observed.
- The existing lock inode checks (regular, single link, same UID,
  no group/other permissions) and `O_NONBLOCK` FIFO protection remain.
- Treat this as an **acquisition-time** integrity check, not
  continuous fencing against a same-user process that later replaces
  the path. The private directory must stay under exclusive server
  control for the whole endpoint lifetime. Windows owner/ACL/reparse
  checks are not yet equivalent.
- `unsafeJournalLockParentsRejected` is a POSIX-only source fixture
  in `physis_admin_journal`. Isolated Linux GCC 14.2 C++20 harness
  built the exact new header blob `3fcd9f029bc7cc71f939c4471828acfbc358b843`
  with `-Wall -Wextra -Werror -pedantic`, and PASSED directory
  permission/symlink, FIFO, inode mode/hardlink, competing lock and
  SIGKILL reacquisition checks. Full native AIVERSE/CTest NOT RUN.
- Handoff: `docs/agent-handoffs/physis-stage46-lock-parent-integrity-2026-11-19.md`.

### Stage 47 — monitor live journal lock ownership (2026-11-19)

- POSIX `JournalProcessLock::valid()` rechecks that the still-published
  lock pathname resolves to the same device/inode as the locked fd, that
  the file remains regular, singly linked, same effective UID and
  private, and that the parent pathname still names the original
  trusted private directory. Successful `acquire()` alone is not enough.
- `LoopbackAdminEndpoint::poll` and `afterTick` check `valid()`
  before privileged operation; lost ownership calls
  `ServerGateway::suspendJournal()`, which is sticky for this endpoint.
  Every endpoint journal write also runs a `valid()` guard; do not
  bypass this writer by directly calling uncoordinated `FileJournal`.
  A lost live lock forbids new submissions and prevents positive
  durability ACKs on status query.
- A same-UID rename/unlink AFTER a check is still a TOCTOU risk:
  regular filesystem path operations plus advisory flock do not give
  distributed fencing, immutable namespace ownership or an atomic
  World + journal checkpoint. Private server-controlled directory
  ownership is mandatory for the full endpoint lifetime.
- POSIX source fixtures `replacedLiveLockSuspendsAdmin` and
  `liveParentPermissionChangeSuspendsAdmin` are registered in
  `physis_admin_journal`, but integrated CMake/CTest are NOT RUN.
- An isolated Linux GCC 14.2 C++20 smoke compiled the EXACT header
  SHA `040fedb623d491428057542eee2f8fae6bcc2a28` using
  `-Wall -Wextra -Werror -pedantic` and PASSED lock rename, parent
  chmod drift, FIFO, symlink, hardlink, conflict and SIGKILL cases.
  This is not a full engine test PASS. Windows `valid()` currently
  checks only `held()`; handle identity/reparse/ACL still needs QA.
- Handoff:
  `docs/agent-handoffs/physis-stage47-live-lock-integrity-2026-11-19.md`.

### Stage 48 — pinned-dirfd privileged journal reads and writes (2026-11-19)

- Main integration prerequisite: verified `main` commit
  `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` is an ancestor of
  `feature/physis-nature-simulation` (GitHub compare: behind 0).
  No redundant merge commit was created and `main` was untouched.
- POSIX `JournalProcessLock` now retains its verified parent directory
  descriptor for the whole lock lifetime; `directoryFd()` returns it
  only while the lock and path identity remain valid.
- `FileJournal::savePinned(dirfd,...)` creates an exclusive random
  temporary file with `openat`, flushes it, performs `renameat` in
  the SAME directory inode and `fsync`s the pinned directory.
  `FileJournal::loadPinned(dirfd,...)` safely opens without symlink
  following, rejects FIFO/non-regular, multi-linked, wrong-owner,
  group/other-accessible and malformed receipt journal files.
- Canonical `LoopbackAdminEndpoint` uses these pinned POSIX journal
  paths for startup and for every persistence write, with a
  `JournalProcessLock::valid()` guard before/after I/O. Windows
  retains the previous path-based journal implementation pending
  ACL/reparse handle-based hardening.
- Direct `FileJournal::save/load/attach` still exist for legacy callers;
  they DO NOT receive endpoint lock ownership. Never bypass the
  canonical endpoint's protected writer in privileged hosts.
- An isolated Linux GCC C++20 smoke compiled the exact new
  `JournalProcessLock` header (blob SHA
  `2512aefb07f612c808041b92a21da897df4f195b`) and PASSED
  the old lock security suite plus pinned directory identity and
  path-rename redirection smoke. This test does NOT compile the full
  `FileJournal` or AIVERSE integration.
- New `pinnedJournalSaveDoesNotFollowMovedParent` and
  `pinnedJournalLoadRejectsUnsafeInodes` fixtures are authored in
  `tests/physis_admin_journal_test.cpp`; native CMake/CTest have
  NOT RUN. Handoff:
  `docs/agent-handoffs/physis-stage48-pinned-journal-io-2026-11-19.md`.

### Stage 49 — independently tested POSIX journal filesystem layer (2026-11-19)

- Canonical POSIX `FileJournal::loadPinned/savePinned` retain the same
  v1 receipt serialization, checksums, IDs, file limits and
  `ServerGateway::restoreJournal`, but delegate raw file operations
  to `engine/physis/PhysisJournalPosixIO.h`. The new
  `posix_journal::read/write` helpers are independent of ECS, net,
  editor, Box3D and game genres.
- A verified directory fd remains mandatory for privileged operations.
  `read` distinguishes a genuinely absent file (fresh journal)
  from a symlink, FIFO, hardlink, unsafe file modes, wrong owner,
  invalid size, and short read. It does not overwrite output on
  failure. `write` uses caller-provided cryptographic random suffix,
  `openat(O_EXCL)`, file fsync, `renameat`, directory fsync and
  `unlinkat` cleanup on failed pre-rename writes.
- New native POSIX CTest `physis_journal_posix_io` is included in
  CMake and explicit Ubuntu/macOS PHYSIS workflow build targets.
- **ACTUAL PASS**: isolated source-identical Linux GCC 14.2 C++20
  CMake/CTest `physis_journal_posix_io` 1/1, GCC ASan/UBSan test,
  and Clang 17 strict-warning standalone binary. Git blobs:
  `PhysisJournalPosixIO.h` =
  `834a651f38b2d9c3ce75bc261b3ec731d3572e8a`,
  `physis_journal_posix_io_test.cpp` =
  `d45e0dd9e5ab5d8d55b806e0b847fe49ced22775`.
- This is NOT a successful full AIVERSE configure/CTest. The entire
  repository cannot be cloned in the local environment (GitHub DNS
  unavailable); GitHub Actions jobs for the feature branch were
  queued or superseded/cancelled, not confirmed green.
- Continue the canonical `LoopbackAdminEndpoint` lock ownership
  and journal error fail-closed policy. Direct legacy path-based
  FileJournal::save/load remain separate. Windows stays on its
  previous implementation. NEREID, TerraWeave and Box3D ownership
  remains unchanged. Handoff:
  `docs/agent-handoffs/physis-stage49-posix-journal-test-2026-11-19.md`.

### Stage 50 — post-read identity and journal collision safety (2026-11-19)

- Canonical `posix_journal::read` now rechecks the opened journal fd,
  visible filename device/inode, file size/owner/mode/link count and
  the pinned directory's ownership BEFORE publishing read bytes.
  This detects common rename/replacement/truncation races. On failure
  caller output remains unchanged; the canonical host fails closed.
- `posix_journal::write` revalidates private directory ownership
  after directory fsync. If chmod changes mid-write, durability is
  reported uncertain instead of success, and ServerGateway's sticky
  journal health policy suspends further privileged edits.
- Native `physis_journal_posix_io` CTest now covers unmodified output
  on invalid/oversized reads, temp-name O_EXCL collision and original
  receipt preservation, and directory mode denial for reads as well
  as writes. Do not silently remove or overwrite a collided temp file.
- ACTUAL PASS: rebuilt local source-identical isolated PHYSIS POSIX
  CTest on Linux (1/1), plus GCC 14.2 and Clang 17 strict-warning
  builds and GCC ASan/UBSan, all passing.
  Verified GitHub blob hashes:
  `engine/physis/PhysisJournalPosixIO.h` =
  `fc1b07631c5191e8681992fb3743482029c7d2ea`,
  `tests/physis_journal_posix_io_test.cpp` =
  `f3500d80dc7c99c389b12e0d8a40721e202d8048`.
- Full AIVERSE compilation and `physis_admin_journal` plus complete
  PHYSIS CTest are STILL NOT RUN; GitHub feature CI is queued/cancelled,
  not confirmed green. No cross-file journal+World atomic guarantee
  or Windows ACL parity. See
  `docs/agent-handoffs/physis-stage50-post-read-integrity-2026-11-19.md`.

### Stage 51 — fail-closed pinned admin startup and NUL-safe paths (2026-11-19)

- The canonical `LoopbackAdminEndpoint::open` must not bind the listener
  if its pinned journal fails to load. Treat corrupt checksum, unsafe
  owner/mode, a symlink, or FIFO as startup failures. Release the
  process lock when opening fails; allow operator remediation and
  a fresh endpoint on the same journal path.
- New POSIX regression `pinnedEndpointRejectsUnsafeStartupJournal`
  in `tests/physis_admin_journal_test.cpp` exercises the real admin
  endpoint: clean startup, checksum corruption refusal, unlocked
  failed startup, symlink/FIFO denial and post-repair restart.
  Its source is authored; native full-engine CTest is NOT RUN.
- POSIX `posix_journal::validBasename` and `write` reject
  embedded NUL bytes in filenames and temporary suffixes; POSIX
  C interfaces would otherwise interpret a truncated name.
  `tests/physis_journal_posix_io_test.cpp` covers these cases.
- ACTUAL PASS: an isolated source-identical Linux CMake/CTest
  `physis_journal_posix_io` 1/1, strict GCC/Clang builds and GCC
  ASan+UBSan. Checked GitHub blobs: I/O header
  `3e00b8ec1508c438f401c240eaa3aa38ab90bbd1`,
  I/O test `a2393a8d09a75429d9e291a537682c72dff09e3f`.
  This is NOT a PASS of `physis_admin_journal`.
- Full native AIVERSE build is still blocked by GitHub DNS from the
  current container and PHYSIS GitHub Actions remain queued/cancelled.
  Keep PR #25 DRAFT, preserve the separate FileJournal + ECS
  checkpoint authority and exact per-stage handoff:
  `docs/agent-handoffs/physis-stage51-pinned-startup-2026-11-19.md`.

### Stage 52 — journal startup revalidation and after-rename durability fault (2026-11-19)

- On canonical `LoopbackAdminEndpoint::open`, after a successful
  `FileJournal::loadPinned` and BEFORE binding `WsListener`,
  revalidate `JournalProcessLock::valid()`. A lock or parent
  replaced during the bounded startup read must fail startup
  closed and release the lock. No privileged listener may bind.
- The native `physis_journal_posix_io` test now has Linux-only
  `O_PATH` fault injection: `openat`/`renameat` operate relative
  to an O_PATH directory descriptor, but `fsync(directory)` fails
  with EBADF. `posix_journal::write` MUST return false despite a
  visible new receipt. Do not advertise `Applied` or durable
  `Queued` after such a failure.
- ACTUAL PASS (isolated): compiled source-identical low-level POSIX
  CTest 1/1 on GCC 14.2; Clang 17 C++20 strict-warning run and
  GCC ASan/UBSan also passed. Test source GitHub blob
  `c9c7063481b15d9e3d96fc3c8b022e86a32f30ff`.
  Full AIVERSE CMake, `physis_admin_journal` integration and
  macOS/Windows QA remain NOT RUN.
- Changes are independent of FPS/RTS, Box3D, NEREID and TerraWeave.
  Handoff:
  `docs/agent-handoffs/physis-stage52-startup-revalidation-fsync-2026-11-19.md`.

### Stage 53 — verify the journal inode after atomic replacement (2026-11-19)

- After successful `openat(O_EXCL)` and exact bytes written to the temp
  receipt file, `posix_journal::write` records its `fstat` identity
  and checks that the staged inode is regular, single-linked,
  current effective UID-owned, private, and exactly the expected size.
- After `renameat`, but before reporting a durable positive
  receipt, the published basename must still refer to that same
  device/inode and secure metadata. Any mismatch, directory fsync
  failure, or loss of parent ownership is an **uncertain** outcome:
  return false to canonical `FileJournal::savePinned`; the gateway's
  journal failure policy suppresses positive Queued/Applied ACKs.
- This is one more acquisition/write boundary check; do NOT claim
  that same-UID filesystem races or journal+World crash atomicity
  are eliminated. Trusted private journal directory remains required.
- Native POSIX `physis_journal_posix_io` CTest now asserts the
  published atomic replacement has a new inode with correct
  owner, mode, link count and no remaining temporary file.
- **ACTUAL PASS**: locally reconstructed exact committed files in
  `/mnt/data/physis49_smoke`; CMake/CTest 1/1, GCC 14.2 strict,
  Clang 17 strict, GCC ASan+UBSan. GitHub blob SHAs:
  `PhysisJournalPosixIO.h` =
  `2dd114731b498c4156da305915201f97c4848647`;
  `physis_journal_posix_io_test.cpp` =
  `7c6cd0ef9e330be5b61bf7935bb8d8b0f025a907`.
- Full AIVERSE native CMake, `physis_admin_journal`,
  `physis_remote_edit`, `physis_field_runtime`, macOS and
  Windows are **NOT RUN/NOT VERIFIED**. See
  `docs/agent-handoffs/physis-stage53-published-inode-2026-11-19.md`.

### Stage 54 — compiled lock + pinned journal I/O integration without ECS (2026-11-19)

- New standalone native test `tests/physis_journal_locked_io_test.cpp`
  compiles the **real** `PhysisJournalProcessLock.h` and
  `PhysisJournalPosixIO.h` together and is registered as CTest
  `physis_journal_locked_io` under
  `AIVERSE_BUILD_TESTS AND UNIX AND NOT EMSCRIPTEN`.
- It acquires a valid journal lock, saves/loads through the borrowed
  pinned directory fd, rejects a concurrent owner, renames the live
  lock inode, checks `valid()==false` and `directoryFd()==-1`,
  demonstrates that a second owner can lock the newly created
  basename, then verifies release/restart ownership and data.
  This confirms why the canonical endpoint's validity checks
  around I/O and gateway suspension must NEVER be bypassed.
- The existing POSIX I/O unit test and the new cross-header test
  are both included in `.github/workflows/physis.yml` targets.
- ACTUAL PASS: source-identical isolated Linux GCC C++20 CMake/CTest
  1/1 for `physis_journal_locked_io`, Clang 17 with strict warnings,
  and GCC ASan/UBSan. Exact code-source blob hashes:
  `PhysisJournalProcessLock.h` =
  `2512aefb07f612c808041b92a21da897df4f195b`,
  `PhysisJournalPosixIO.h` =
  `2dd114731b498c4156da305915201f97c4848647`,
  `physis_journal_locked_io_test.cpp` =
  `db075dd14dd47f77a719fa47a86cce4adbac71ae`.
- Full AIVERSE CMake, `physis_admin_journal`, complete PHYSIS CTest,
  macOS, Windows, editor and production WebSocket remain NOT RUN.
  Source test proves two standalone headers coexist, NOT full
  authenticated endpoint behavior. See
  `docs/agent-handoffs/physis-stage54-locked-io-test-2026-11-19.md`.


### PHYSIS Stage 55 — interrupted temporary journal recovery (2026-11-19)

- `posix_journal::write` rejects empty payloads, so a bug in a caller
  cannot replace a valid V1 receipt journal with an invalid empty file.
  `FileJournal::savePinned` still uses the unchanged V1 encoder.
- Interrupted writes before `renameat` may leave orphan
  `receipts.next-*` files; these are NOT committed history. On
  startup/read, use only the canonical receipt filename. A future
  writer uses its own cryptographically random O_EXCL temp suffix.
  Do not automatically delete unrecognized orphan files while a
  privileged service is running; keep them for operator inspection.
- `physis_journal_posix_io` now exercises orphan-preserving recovery,
  empty-payload rejection and normal subsequent publication. Isolated
  Linux native CTest 2/2 with the existing locked I/O test, GCC 14.2,
  Clang 17, ASan and UBSan PASSED. Full engine CTest/CI did NOT run.
- Handoff: `docs/agent-handoffs/physis-stage55-interrupted-temp-recovery-2026-11-19.md`.

### Stage 56 — hard 16 MiB POSIX receipt journal bounds (2026-11-19)

- `posix_journal::read/write` now enforce a 16 MiB upper bound,
  even for direct callers bypassing `FileJournal`.
  Reject `minBytes>maxBytes`, `maxBytes>kMaxJournalBytes`, empty
  and oversize writes BEFORE filesystem changes.
- Canonical `PhysisRemoteJournal.h` now has `static_assert`
  ensuring serialized `kMaxDiskBytes` and the low-level POSIX cap
  are equal; preserve this invariant whenever changing formats.
- `physis_journal_posix_io` asserts oversized writes leave
  `receipts` and temp paths untouched, and invalid read bounds
  preserve the output buffer.
- **ACTUAL PASS**: source-identical isolated GCC 14.2 C++20
  CMake/CTest 2/2, Clang 17 strict warnings and GCC ASan/UBSan.
  GitHub blobs: low-level header
  `ab24c98afebc3dbe45a05244b1a94e6eec977ccf`, test
  `d3ef56264ab8da3324375e7b8d76bf1a1ea64851`.
- **NOT RUN**: full AIVERSE CMake and `FileJournal` static_assert
  compile, `physis_admin_journal`, `physis_remote_edit`,
  `physis_field_runtime`, macOS/Windows and complete CI.
  Handoff:
  `docs/agent-handoffs/physis-stage56-bounded-posix-journal-2026-11-19.md`.

### Stage 57 — independent PHYSIS journal CI (2026-11-19)

- Added a second, **independent** POSIX CMake entry point at
  `tests/physis_journal_standalone/CMakeLists.txt`. It builds both
  real production-header regressions without configuring the full
  AIVERSE engine, ECS, net, editor, terrain, water or Box3D.
- `.github/workflows/physis.yml` now has a separate Ubuntu/macOS
  `physis-journal-standalone` job (configure, compile with strict
  warnings, CTest, upload logs), alongside the unchanged
  `physis-native` full integration matrix. The new CMake folder is
  included in CI trigger paths.
- **ACTUAL PASS**: local native Linux build USING THE EXACT new
  CMake source (Git blob
  `f4e60d18a2db0e8b40fb3d0f996a1cb31145d629`), GCC 14.2,
  C++20 and -Wall -Wextra -Werror -pedantic: 2/2 CTests pass.
  All four test/header blob SHAs match repository copies.
- **NOT RUN**: complete root AIVERSE CMake/CTest, Mac runner,
  integrated `physis_admin_journal` and `physis_remote_edit`.
  GitHub Actions on PHYSIS were queued/cancelled when inspected;
  no completed full-engine PASS. The repo also has an existing
  PR #25 conflict with its feature base; user previously explicitly
  requested only main ancestry (main is already included).
- Handoff:
  `docs/agent-handoffs/physis-stage57-standalone-ci-2026-11-19.md`.

### Stage 58 — journal workflow isolation (2026-11-19)

- The Stage 57 journal test job was inside `.github/workflows/physis.yml` and thus shared its workflow-level concurrency cancellation group with the full-engine PHYSIS job. A new code/doc push repeatedly cancelled BOTH jobs before runners started. This was not truly independent CI.
- Stage 58 moves the standalone journal Ubuntu/macOS test job to `.github/workflows/physis-journal.yml`, with its own concurrency group and triggers specific to journal sources/tests and its CMake. The original `physis.yml` retains the complete native PHYSIS/Box3D matrix unchanged.
- New workflow source commit: `0d213e8f91a08b81eadd0d0f2eb847474444ad34`. GitHub created TWO distinct queued workflow runs at this SHA: `44495750669` (journal standalone) and `44495750438` (full PHYSIS).
- ACTUAL PASS: local Linux CMake/CTest using exact standalone project, `physis_journal_posix_io` and `physis_journal_locked_io` **2/2**. GitHub runner jobs are still QUEUED, not PASS; full engine, Ubuntu/macOS CI and editor integration remain unverified.
- Keep per-stage handoff at `docs/agent-handoffs/physis-stage58-independent-ci-2026-11-19.md`. PR #25 remains DRAFT and has merge conflicts with its target feature branch; main is already included and must not be remerged unnecessarily.

### Stage 59 — actual SIGKILL cross-process receipt recovery (2026-11-19)

- Added tests/physis_journal_crash_recovery_test.cpp using real
  JournalProcessLock and posix_journal::read/write in separate POSIX
  processes, with pipe+poll handshake and SIGKILL.
- The child writes a canonical journal and fsyncs a different
  .next-uncommitted temporary file BEFORE rename. SIGKILL then
  releases flock without a destructor. The parent reacquires the
  lock, reads only the committed canonical bytes, writes a new
  journal, and preserves the orphan for inspection.
- The test is registered in root CMake and the standalone CMake,
  and included in the independent Ubuntu/macOS journal workflow
  and the full PHYSIS build target list.
- ACTUAL PASS: source-identical local Linux standalone GCC 14.2
  native CMake/CTest 3/3; Clang 17 strict C++20 PASS; GCC
  ASan+UBSan PASS. Exact new CTest source blob:
  644671c632d0a2cc33372263681754cc6843bcb1.
- NOT RUN: full AIVERSE root CMake, high-level FileJournal V1
  codec, PHYSIS admin WebSocket/ECS, Ubuntu/macOS CI completion,
  Windows, Box3D or editor. This test protects the low-level
  process/IO boundary, not exactly-once gameplay effects.
- Handoff: docs/agent-handoffs/physis-stage59-process-crash-recovery-2026-11-19.md.

### Stage 60 — prevent agent documentation commits cancelling PHYSIS CI (2026-11-19)

- `.github/workflows/physis.yml` previously included the entire
  `engine/physis/**` path in both push and PR triggers. Each mandatory
  handoff update to `engine/physis/AGENTS.md` retriggered full CI
  and `cancel-in-progress` cancelled queued Ubuntu/macOS jobs.
- Excluded ONLY `!engine/physis/AGENTS.md` AFTER its positive
  `engine/physis/**` pattern for BOTH push and pull_request
  path lists. Changes to actual PHYSIS C++, tests, CMake and the
  workflow itself still trigger the native matrix. The standalone
  journal workflow stays independent and already excludes docs.
- Code commit: `96b1efb210f9f4f9f050415e5e936b2707c1a500`.
- ACTUAL isolated Linux test re-run using exact current repository
  source blobs: CMake/CTest 3/3 PASS. Clang 17 strict native C++20
  builds/runs all 3 PASS; GCC 14.2 ASan/UBSan crash recovery PASS.
  The workflow exclusion was verified from fetched source; its real
  GitHub trigger behavior is still awaiting a runner event.
- Full engine, `physis_admin_journal`, `physis_remote_edit`,
  `physis_field_runtime` and real Ubuntu/macOS CI are NOT PASSED.
- Per-stage handoff:
  `docs/agent-handoffs/physis-stage60-ci-doc-filter-2026-11-19.md`.


### Stage 61 — reproducible focused native PHYSIS test runner (2026-11-19)

- New executable `tools/physis-validate-native.sh` provides a
  macOS Bash 3.2-compatible one-command validation path. Default
  `--full` configures root AIVERSE with tests and physics ON, builds
  all headless PHYSIS targets explicitly, and runs CTest
  `-R '^physis_'`. It excludes editor-core authoring fixtures by
  design; these need a separate editor-enabled run.
- `--standalone` configures the real
  `tests/physis_journal_standalone` project and builds/runs its
  three POSIX journal tests. Options include `--jobs N`,
  `--no-physics` (full only) and `--build-dir DIR`.
  It never deletes a build directory and always records stage logs,
  failure exit statuses and a summary under
  `<build-dir>/physis-reports/<timestamp>-<pid>/`.
- **ACTUAL PASS**: ran `bash -n` on the exact source and invoked
  `--standalone --jobs 2 --build-dir build/scriptcheck` from outside
  the repo. GCC 14.2 native CMake/CTest **3/3 Passed**, exit 0.
  The script blob SHA `9880c7aefc0ddf4791e69d729abf289729a3a96b`
  matches the GitHub commit.
- **NOT RUN**: `--full`, AIVERSE engine CMake, integrated admin
  gateway, macOS, Box3D and full PHYSIS CTests: the current container
  cannot resolve github.com for full checkout and GitHub Actions
  native jobs remain queued. Do not interpret standalone PASS as
  an engine PASS.
- Handoff:
  `docs/agent-handoffs/physis-stage61-native-validation-runner-2026-11-19.md`.

### Stage 62 — targeted integration-mode PHYSIS validation (2026-11-19)

- `tools/physis-validate-native.sh` gained `--integration`, a
  focused root AIVERSE CMake mode that builds ONLY
  `aiverse_physis_admin_journal_test`,
  `aiverse_physis_remote_edit_test`,
  `aiverse_physis_field_runtime_test`, and the three standalone
  POSIX journal targets, then executes the six matching
  `^physis_(...)$` CTests. This avoids building all PHYSIS
  environmental/disaster/Box3D targets during debugging.
- For quickest PHYSIS admin tests on a Mac with full checkout:
  `bash tools/physis-validate-native.sh --integration --no-physics --jobs 4`.
  Existing `--standalone` and `--full` modes remain.
  Failure exits nonzero with persistent configure/build/ctest logs.
- **ACTUAL PASS**: exact script SHA
  `9125e955165a664e0759b0bdab556e23157783ff`;
  `bash -n` success. Standalone real POSIX CTest **3/3 pass**.
  Integration-mode flow was executed in a temporary separate fake
  CMake fixture with six stub target names: simulated 6/6 PASS;
  injected failure returned exit 8 and wrote FAIL ctest log.
  This checks script orchestration ONLY, NOT AIVERSE integration!
- Checked each actual integration target and six test names exists
  in repository root `CMakeLists.txt`. Full engine CMake/CTest
  including `physis_admin_journal` remains NOT RUN because the
  local container cannot clone GitHub; GitHub Actions Ubuntu/macOS
  runner jobs still queued.
- Handoff:
  `docs/agent-handoffs/physis-stage62-targeted-integration-runner-2026-11-19.md`.

### Stage 63 — revision probe must not accept an unhealthy journal (2026-11-19)

- `RemoteClient::poll` now checks `journalRetrySuspended()` on the
  **revision-probe** `Result::Unknown` path, not only on outstanding
  edits. The authoritative ECS revision number can still be visible
  while the durable dedupe journal is unhealthy. Do NOT mark
  `hasRemoteRevision()` true in that case; keep it false, preserve
  the server diagnostic and enter `ClientStatus::Uncertain`.
- The operator can call `requestRevision()` explicitly after a
  newly healthy server is available. A normal Unknown response
  provides its current ECS revision, sets `hasRemoteRevision()`
  true, transitions the no-edit client to Idle and clears the
  obsolete journal failure message. Never automatically retransmit
  edits just because a probe recovered.
- Added `revisionProbeStopsOnUnhealthyJournal()` to the existing
  `tests/physis_remote_edit_test.cpp`. It exercises a REAL
  ServerGateway with `suspendJournal()`, a real RemoteClient,
  the loopback `net::Conn`, and a fresh recovered gateway.
  Registered in its existing main; native AIVERSE CTest is
  **AUTHORED BUT NOT RUN**.
- Code commits `67cc79cf73dcd79e46720eaf2be695d5a7abc4fc`,
  `607202a833a8af95e27668ebe27ba0cd3fddf9c1`,
  `369d24bd379556007cbe2e7234cb20ab85cb3496`,
  `a0f078d738a1636fa41309911edc30a6b81bf67c`.
  Current blobs: client
  `872e61c380fee748cf219341caa6178d35fd5de1`,
  test `0c9eaaa6c95f2e37a15ee7180cacacbd283cfc9f`.
- **ACTUAL PASS**: the unaffected standalone POSIX journal suite was
  rerun on Linux (CMake/CTest **3/3**). It does NOT compile or test
  RemoteClient. Full AIVERSE CMake, `physis_remote_edit`,
  `physis_admin_journal`, macOS/Windows are NOT RUN; CI runner
  jobs were queued/cancelled, and local git clone fails DNS.
- Handoff:
  `docs/agent-handoffs/physis-stage63-revision-probe-durability-2026-11-19.md`.

### Stage 64 — monotonic remote receipts and safe Applied re-verification (2026-11-19)

- `RemoteClient` now ignores late Queued/Unknown/Rejected/Conflict/Denied
  receipts after a terminal result on the same connection. A reconnect
  may explicitly re-verify an Applied request using a Query for its SAME
  request ID, but ignores pre-query replies and stale Queued while verifying.
- If the authoritative server no longer knows a previously Applied
  request, the client becomes Uncertain and requires an explicit
  operator retry. It cannot automatically re-Submit after that warning,
  or start a NEW edit while uncertain/re-verifying.
- Applied verification Query timeout can be retried manually using
  Query only, never Submit. The previously Applied request ID is
  preserved; server ACL, saved ECS revision and journal remain authoritative.
- Three source regression fixtures in `tests/physis_remote_edit_test.cpp`:
  `delayedReceiptCannotUndoApplied`,
  `appliedRecheckNeedsFreshQueryAndManualReconcile`, and
  `appliedRecheckTimeoutCanRetryOnlyQuery`.
- Implementation commits: `79890dc1c74e34b97325d90b7a09d55c17c14497`,
  `1e5d9e282f0b0720d7ba570ba2866efba1afd76e`.
  Docs/test follow-up: see the commit containing this handoff.
- **Verification:** connected GitHub source/readback checked. Native
  full AIVERSE build, `physis_remote_edit` CTest and editor/UI QA
  **NOT RUN** (no full checkout: container DNS fails on github.com);
  PR workflow lookup for implementation HEAD returned no runs/checks,
  NOT a PASS. Keep PR #25 Draft. The wire V1 protocol has no Query
  correlation nonce: fully formal cross-reconnect freshness remains
  future work. No schema changes, server auth bypass, genre coupling,
  NEREID/TerraWeave/Box3D edits.
- Full handoff:
  `docs/agent-handoffs/physis-stage64-monotonic-ack-2026-11-19.md`.

### Stage 65 — integration regressions fixed and user-verified on macOS (2026-11-19)

- On the previous Stage 64 checkout, the user reported native CTest **4/6**:
  - PASS `physis_journal_posix_io`, `physis_journal_locked_io`,
    `physis_journal_crash_recovery`, `physis_field_runtime`;
  - FAIL `physis_admin_journal` and `physis_remote_edit`.
- Fix commit `e628ba359a31be4e8fa2d16d8fab156e1e212e4d`:
  - `RemoteClient::poll` keeps a previously confirmed `Applied`
    status while merely sending a reconnect Query. Incoming
    authoritative response/timeout still controls the eventual state.
  - In the native loopback WebSocket admin test, the time passed to
    `WsConn::update` stays monotonic between synthetic authentication
    and edit loops, preventing unsigned timeout underflow.
    Failure-only admin/WS diagnostics were added.
- **ACTUAL macOS user-reported PASS AFTER FIX**: integration runner's
  headless C++ build succeeded, then all six native CTests PASS
  (`0 failed out of 6`, total 2.99 seconds). Runner summary:
  `PHYSIS validation PASS`, report
  `/Users/aleksandrfeer/Projects/AIVERSE/build/physis-validation-integration/physis-reports/20261119-102507-474`.
- This is verified test output supplied by the user, not a local
  test executed by this agent. Full PHYSIS/Box3D/Editor/Windows/CI
  suites are still **NOT RUN/NOT CONFIRMED**. PR #25 stays Draft.
- Next: on the same macOS checkout, run
  `bash tools/physis-validate-native.sh --full --jobs 4`,
  then editor-enabled PHYSIS authoring targets. Do not claim
  production readiness until complete QA.
- Standalone handoff:
  `docs/agent-handoffs/physis-stage65-macos-integration-fixes-2026-11-19.md`.

### Stage 66 — fixed-point gravity expectation regression on macOS (2026-11-19)

- User ran `bash tools/physis-validate-native.sh --full --jobs 4`
  at commit `3a1c1536b904dcdc6174e2054ab6ed97ea20e0a2`
  (AppleClang 17, macOS x86_64 reported by CMake):
  **configure PASS, build PASS, native PHYSIS CTest 23/24 PASS**.
  Only `physis_forces` failed, at
  `tests/physis_forces_test.cpp:34` on direct equality
  `sampled.gravityForce.y==Fixed::fromRatio(-981,10)`.
  `physis_box3d`, journal, network, editor view and other
  22 targets passed.
- Root cause: Q47.16 `fromRatio(-981,100)` quantizes acceleration
  to raw `-642908`; multiplying by mass 10 yields
  raw `-6429080`, whereas independently quantizing
  `fromRatio(-981,10)` gives raw `-6429082`.
  Difference is exactly two Q47.16 raw units, **not** a physics error.
- Source fix commit `2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90`
  changes ONLY `tests/physis_forces_test.cpp`:
  assert exact quantized `gravityAcceleration==gravity.baseline`,
  exact deterministic `gravityForce==gravity.baseline * massKg`,
  plus within-two-raw-units approximation to ideal -98.1 N.
  No PHYSIS solver/API, Box3D adapter, ECS save, network, Water or
  Terra changes.
- **Verification:** full 24-target run is real macOS user-provided
  build/CTest evidence BEFORE this test correction; revised test is
  **NOT YET RUN** on macOS. Do not claim 24/24 until user reruns.
  Suggested focused command:
  `ctest --test-dir build/physis-validation-full --output-on-failure -R '^physis_forces$'`
  AFTER compiling the changed target; use the full runner for a
  complete 24/24 confirmation.
- Handoff:
  `docs/agent-handoffs/physis-stage66-fixed-gravity-rounding-2026-11-19.md`.

### Stage 67 — full headless PHYSIS green on macOS (2026-11-19)

- After Stage 66's Q47.16 gravity force test correction (`2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90`), the user reran the native `tools/physis-validate-native.sh --full --jobs 4` on macOS.
- **ACTUAL USER-REPORTED PASS:** CTest **24/24 passed, 0 failed**, 10.51 seconds; PHYSIS validation PASS. The previous sole failure `physis_forces` is therefore resolved. PHYSIS/Box3D, integration, networking, persistence, security and headless editor-tagged tests passed in this runner.
- Report: `/Users/aleksandrfeer/Projects/AIVERSE/build/physis-validation-full/physis-reports/20261119-105206-10283`.
- This result does **not** include the separate `physis_authoring` and `physis_field_authoring` CTests: in CMake both require `aiverse_editor_core`, which is enabled by `AIVERSE_BUILD_EDITOR` or `AIVERSE_BUILD_TOOLS`.
- Next validation on macOS, using a separate build directory with editor-core enabled (tools ON, GUI editor OFF):
```sh
cmake -S . -B build/physis-editor-validation -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF
cmake --build build/physis-editor-validation --target aiverse_physis_authoring_test aiverse_physis_field_authoring_test --parallel 4
ctest --test-dir build/physis-editor-validation --output-on-failure -R '^physis_(authoring|field_authoring)$'
```
- The authoring suite, GUI interaction, full repository-wide CTest and completed cross-platform CI remain unverified. PR #25 stays Draft until remaining integration and review.
- Standalone handoff: `docs/agent-handoffs/physis-stage67-headless-green-2026-11-19.md`.

### Stage 68 — macOS PHYSIS Editor authoring native CTests PASS (2026-11-19)

- User ran the editor-core enabled native configuration `build/physis-editor-validation` after pulling PHYSIS branch at Stage 67. The two PHYSIS MapDoc authoring CTests both PASSED:
  - `physis_authoring` (#119), 3.28s.
  - `physis_field_authoring` (#128), 2.70s.
  - **2/2 Passed; 0 failed**; CTest label summaries EDITOR and PHYSIS both 5.99 sec*proc.
  - The `aiverse_physis_field_authoring_test` target explicitly compiled and linked successfully.
- Together with the previous actual macOS `--full` run (24/24 PASS), **26 PHYSIS test executions passed across two distinct CMake builds**. Do not misrepresent this as one 26-test CTest run.
- No runtime or editor C++ code was changed this stage; this is test evidence/documentation only. GUI mouse interaction, Scene Gizmo visual behavior, Inspector Undo/Redo clicks, full repository-wide CTest, completed platform CI, native external admin TLS remain NOT VERIFIED.
- Next: build/launch the actual macOS editor with PHYSIS enabled and manually validate authoring Scene Gizmos, MapProps/Inspector Undo/Redo, and read-only overlays. Then collect complete CI/check results; keep PR #25 Draft until validation and review.
- Handoff: `docs/agent-handoffs/physis-stage68-editor-authoring-green-2026-11-19.md`.

### Stage 69 — PHYSIS GUI editor transport namespace collision (2026-11-19)

- First real macOS GUI-editor build failed at `apps/editor/windows/PhysisWindow.cpp` with **12 errors and one cascading unused-field warning**. `aiverse::editor::net` shadows `aiverse::net` inside `aiverse::editor::physis_remote` / `aiverse::editor`.
- Production fix: in `apps/editor/PhysisRemoteEdit.h`, explicitly qualify every `::aiverse::net::Key`, `Conn`, `Bytes`, `ConnState`, and `wipe`; in `apps/editor/windows/PhysisWindow.cpp`, qualify `::aiverse::net::nowMs()`. Do not change the separate `editor::net` namespace, network security, or ECS behavior.
- Compile-regression fixture: `tests/physis_remote_edit_test.cpp` declares a synthetic `aiverse::editor::net` namespace **before** including EditorLink. Its existing native CTest target now detects future accidental unqualified transport symbols in the bridge.
- Real native PHYSIS headless 24/24 and editor authoring 2/2 passed **before this code fix**. **GUI rebuild and new collision fixture have NOT RUN after the fix**. The new source was inspected, not compiled by this agent. The Stage68 user GUI build is the precise failure evidence.
- Next: pull PHYSIS branch, rebuild `aiverse_physis_remote_edit_test` and run CTest `physis_remote_edit` in `build/physis-editor-validation`, then rebuild `aiverse_editor` from `build/physis-gui-validation`; preserve real compiler output and report any further failures.
- Handoff: `docs/agent-handoffs/physis-stage69-editor-net-shadow-2026-11-19.md`.

### Stage 70 — repair ternary separator after qualified network namespace (2026-11-19)

- Real user macOS native build of `aiverse_physis_remote_edit_test` after Stage69 failed at `apps/editor/PhysisRemoteEdit.h:169` with two parser errors: `expected ':'`, `expected unqualified-id`. Source had `return client_?client_->exportOutstanding():::aiverse::net::Bytes{};`. Bulk qualification had concatenated the ternary's `:` with the absolute namespace `::`.
- Fix commit `4817aaadebe20dd9f77e2d057d3ff275848e1416`: `return client_ ? client_->exportOutstanding() : ::aiverse::net::Bytes{};`. This retains the Stage69 correct explicit transport namespace and changes only one return statement.
- Re-fetched bridge header and checked that no `:::` remains in `PhysisRemoteEdit.h`, `PhysisWindow.cpp` or `tests/physis_remote_edit_test.cpp`. **Actual post-fix native compilation/CTest/GUI build NOT RUN**. Previously reported PHYSIS 24/24 and editor authoring 2/2 were obtained before these Stage69/70 source edits.
- Existing `physis_remote_edit` translation unit has a deliberate `aiverse::editor::net` namespace shadow fixture to cover the bridge's qualified symbols, but must actually be compiled on macOS to confirm.
- Handoff: `docs/agent-handoffs/physis-stage70-ternary-net-qualification-2026-11-19.md`.

### Stage 71 — native macOS editor GUI build succeeds (2026-11-19)

- **ACTUAL user-reported native results after Stage70:** `physis_remote_edit` CTest **1/1 PASS** (0.08 sec), then `aiverse_editor` built successfully to **100%**. The build includes `aiverse_editor_core`, `aiverse_net`, runtime, rendering and FPS dependencies. This confirms the Stage69 net namespace shadow and Stage70 ternary parser errors no longer block compilation/linking.
- Earlier native runs passed PHYSIS headless 24/24 (with Box3D) and editor MapDoc authoring 2/2. They were run before Stage69/70 changes; current native compile/regression evidence is the fresh 1/1 plus GUI editor build.
- **No new C++ source code changed** in this stage; this is the mandatory documentation/handoff stage.
- The PHYSIS editor window is registered under `world`, titled `PHYSIS · Природа`, **closed by default**. Its demo runs in a private World. Real PHYSIS nature sampling requires a GameModule that installs PHYSIS. A 3D scene overlay only reads a live PHYSIS world. Remote editing requires explicit authenticated host attachment.
- **NOT verified:** launching the GUI, visual layout, mouse selection/marquee, field gizmos/snap/group transform, Undo/Redo, remote admin login, full repository-wide CTest, completed CI, Windows and TLS/WSS. Keep PR #25 DRAFT.
- Next: launch app from `build/physis-gui-validation` and perform manual visual checks; do not claim GUI QA success without evidence. Use:
```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git switch feature/physis-nature-simulation &&
git pull --ff-only origin feature/physis-nature-simulation &&
APP="$(find build/physis-gui-validation -name AiverseEditor.app -type d -prune -print -quit)" &&
test -n "$APP" &&
open "$APP"
```
- Handoff: `docs/agent-handoffs/physis-stage71-macos-gui-build-green-2026-11-19.md`.

### Stage 72 — actual main merge carries NEREID water (2026-11-19)

- Source head merge: `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b`, parents PHYSIS
  `04f3fae2576edf92aeddfcd3ab3f6aab77d1d8e7` and main
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`. Verified behind main 0 after merge.
- Three-way file integration against common ancestor
  `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`:
  97 independent main file changes imported, 12 overlapping paths
  resolved. Merged CMake target registration, AGENTS roles and
  generated skills catalog (220 unique ids). Retained advanced
  PHYSIS SpectraFX files and selected the main VFX ECS regression
  preventing unsigned particle-count underflow.
- Canonical NEREID water APIs are now in the branch at
  `libraries/include/aiverselib/water/`; renderer/scene integration
  is at `engine/world/Nereid*.h`, `engine/render/Nereid*.h`.
  `engine/world/NereidPhysisWindBridge.h` is visual only.
  **No duplicate PHYSIS water authority**: use
  `InstallOptions::floodMode=FloodMode::ExternalAuthority` when
  NEREID water is canonical. The host controls installation and
  deterministic scheduling.
- GitHub static source checks PASS: two merge parents, main ancestry,
  CMake NEREID and PHYSIS targets present once, skill JSON valid with
  220 unique entries including PHYSIS + 6 NEREID skills.
- **NOT RUN for the combined merge**: native C++ compile, CTest,
  GPU/Metal water rendering, editor QA, network/remote admin integration.
  Earlier individual subsystem CTests do not cover this merge.
- Standalone handoff:
  `docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.

### Stage 73 — post-merge PHYSIS + NEREID + SpectraFX focused CTest GREEN (2026-11-19)

- **Actual user-reported native macOS post-merge CTest:** 40/40 tests passed, 0 failed, total wall time 10.25s. The test command from Stage72 selected `^(physis_|ecs_nereid_|vfx_)`. The summary labels included PHYSIS 26 tests (21.96 sec*proc), SpectraFX 5 (5.98 sec*proc), EDITOR 3, NETWORK 2, PERSISTENCE 4, PHYSICS 1, SECURITY 3. The 9 remaining tests in the 40-test selection correspond to the `ecs_nereid_` filter; category labels can overlap and must not be added together.
- This verifies native CTest compatibility for the **combined PHYSIS branch after merging main/NEREID** at `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b` and Stage72 docs `012eca843ed65651a31ca48dd7944fa5c3835a37`. It supersedes Stage72's "combined CTest NOT RUN" status.
- The user pasted the terminal summary, **not** independent compiler logs or a saved report directory for the post-merge run. Do not infer all-repository CTest or GUI/GPU QA from this evidence.
- No C++ source or ECS schema changed this stage. PHYSIS remains authoritative for climate/wind/gravity/storms; NEREID is authoritative for water. When combined in the same native World, opt into `FloodMode::ExternalAuthority` for PHYSIS. Existing read-only `engine/world/NereidPhysisWindBridge.h` is presentation only and does not prove real gameplay weather-to-water coupling.
- **Still unverified:** full repo-wide tests, one-host joint PHYSIS+NEREID ECS scheduling/rollback with both systems active, interactive GUI gizmos/Inspector, actual Metal water render/performance, cross-platform CI and admin security/TLS.
- Handoff: `docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

### Stage 74 — full-CTest skill audit and unrelated Place timing (2026-11-19)

The user ran all 150 CTests on the merged PHYSIS+NEREID branch.
**148 PASS, 2 FAIL**: `skill_audit` referenced nonexistent
`tests/physis_remote_edit_test.cpp::revisionProbeStopsOnUnhealthyJournal`
as a path; the actual file exists, the function name is not part
of the path. `place` failed only `CHECK(tickAvg < 5.0)` with a
user-reported 8.204ms tick average (worst 19.381ms), while all
other 51 internal Place scenario checks passed.

Commit `a61923df653994a9a94c1916f4077db7fb7e9848`
splits the PHYSIS skill path and method name, and registers Place
CTest with `RUN_SERIAL TRUE` to prevent concurrent CPU load from
polluting the measured server wall-clock tick. The 5ms threshold
and physics simulation code are unchanged. A minimal native
CMake 3.31 smoke verified the property is recognized. The full
macOS post-fix run is **NOT RUN**: do not claim 150/150 yet.
Keep PR #25 Draft. Record later actual Place timing and
`skill_audit` output; if Place fails serially, profile the
actual CPU/build config instead of raising its threshold.
Handoff: `docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`.

### Stage 75 — AIVERSE progress monitor, isolated Place still above budget

New general `tools/aiverse-progress.py` monitors long CMake/CTest
jobs, printing active tests and elapsed wall time in an interactive
terminal and a periodic heartbeat in non-TTY output. Uses only Python
stdlib; full output/JSON reports are preserved. Source:
`d83716dcd835df40c7861828a92064e961a63bc6`.
Local Linux toy CMake smoke passed with real success/failure/
no-test exit code propagation and a 15s heartbeat; **native macOS
AIVERSE use and TTY visuals NOT YET RUN**.

User-rerun of `place` after Stage74 `RUN_SERIAL`: FAIL
`tickAvg=8.110ms`, target `<5.0`, worst 19.245ms, duration
309.14s; `skill_audit` PASS. The earlier parallel run was
8.204ms, so scheduling isolation is insufficient. Do NOT
raise the threshold; first test Release performance separately,
inspect CPU architecture and profile bottlenecks if needed.
Docs: `docs/aiverse-build-monitor.md`; handoff:
`docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

### Stage 76 — live warning/error panel in AIVERSE monitor (2026-11-19)

- Implemented in universal `tools/aiverse-progress.py`: pinned
  **Warnings / Errors** panel colored red/yellow in TTY,
  separate recent error/warning queues, compressed source location
  labels, stage-spanning counts and an unabridged
  `diagnostics.log`. `summary.json` records stage/total counts.
- Source+regression commit
  `47a320624a118d3f5dd23a7e9d4c81d58e095676`;
  `tools/tests/test_aiverse_progress.py` registered as native
  CTest `tool_aiverse_progress` when Python tests are enabled.
- ACTUAL LOCAL TESTS: Python unittest 3/3; synthetic CMake
  `tool_aiverse_progress` CTest 1/1; synthetic configure/build/
  CTest 2/2 PASS with warning log and **0 false errors**. All
  run on local Linux fixtures (NOT full AIVERSE macOS). Native
  Terminal/iTerm GUI acceptance is still NOT RUN.
- User provided a real macOS `Release` Place benchmark
  `AIVERSE: PASS` in **1:41**, report
  `build/place-release-validation/aiverse-reports/20261119-172509-59587`;
  exact Release `tickAvg` not in summary. Prior **Debug**
  standalone Place 8.110ms FAIL is not evidence Release fails.
  Do not change the original 5ms threshold.
- Handoff: `docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

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

### Stage 79 — deterministic structural stress from existing nature fields

- **Source commits:** `fa0b00d6d60a4fb6b6057610806c1d040fab6897`
  (Stage78 bridge freshness: reject old same-stamp contacts if PHYSIS
  climate has already advanced), `21c016ede31107f48c50ecc5247cfc0cf98ae56a`
  (new structural nature stress policy and test).
- `engine/physis/PhysisNatureStress.h` defines two opt-in ECS
  components (`NatureStressReceiverComponent`,
  `NatureStressOutputComponent`) and
  `advanceNatureStress(World&)`. It consumes only the existing
  `ForceOutput::result.aerodynamicForce` (no re-simulated
  wind, no gravity double-load), matched `ForceReceiver` position
  and same-tick `NatureContactComponent` fire/quake data.
- Output records wind load in Newtons, horizontal bending moment
  in N·m, force-vs-strength ratio, accumulated damage and a single
  fracture transition. Fatigue/recovery/fire/quake coefficients are
  **per PHYSIS climate tick**. Fracture latches; no implicit repair.
  Duplicate same-tick sampling is idempotent and stale/disabled
  inputs invalidate the instant load without clearing prior damage.
- Enable via `InstallOptions::enableNatureStress` ONLY together
  with `enableForces` and `enableNatureContacts`. Both
  stress ECS schemas are opt-in so prior save manifests remain
  unchanged. A native project creates all input and output
  components on interested entities. Host owns movement, physical
  constraints, actual collapse/spawn/destruction and idempotent
  event consumption. Use one stress proxy per foliage cluster
  where necessary, NOT one per grass blade.
- CMake test: `aiverse_physis_nature_stress_test` /
  `physis_nature_stress` with deterministic overload, fire,
  one-time fracture, COW and save/rollback checks.
- **Verification:** code/registration inspected from GitHub;
  no full checkout in execution container (github.com DNS
  unavailable), **no new native compile or CTest executed**.
  Previous 40/40 focused run predates Stages 78/79. Never claim
  code is green until actual tests.
- Handoff: `docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

### Stage79 follow-up — regression fixture syntax (2026-11-19)

Test-only commit `146013f2e27cdefe24d6f224c6e30bad88c22251`
corrected 12 non-deducible `World::add<T>(entity,{})` calls
in `tests/physis_nature_stress_test.cpp` to explicit
`World::add<T>(entity,T{})`. This was discovered by inspecting
the actual generic variadic `World::add<T,Args...>` API
before a native build; it is NOT proof of a compiling/running
test suite. PHYSIS stress system code is unchanged.
Prioritize macOS build of `aiverse_physis_nereid_contacts_test`
and `aiverse_physis_nature_stress_test` and actual CTest output.
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


## Stages 83–85 — fracture repair and durable host dispatch (2026-11-19)

- Stage 82's persisted `NatureFractureOutboxComponent` v1 remains the only PHYSIS source of pending fractures. It does NOT own falling tree rigid bodies.
- Stage 83: `rearmNatureFractureAfterRepair` only after an acknowledged event, a later climate tick and explicit gameplay repair of `NatureStressOutputComponent`. Preserve prior tick/key tombstone and never emit a second event with an identical identity.
- Stage 84: `PhysisFractureDelivery.h::dispatchNatureFractures` is an authoritative host-barrier helper, NOT an installed tick system. `DurablyCommitted` means the host has persisted and deduplicated the external reaction; `Retry` leaves Pending. Host must reconcile physical objects with ECS rollback, handle deleted entities externally and respect callback budget.
- Do not `#include` TerraWeave, NEREID, Box3D or genre headers inside PHYSIS core to implement the bridge; add it to game/world integration once both owners' changes are available.
- Stage 85 adds source regression scenarios in existing `physis_fracture_outbox` CTest; native compiler/CTest/CI not executed by the connector agent. No claims of green validation.
- Handoffs: `docs/agent-handoffs/physis-stage83-fracture-rearm-2026-11-19.md`, `physis-stage84-fracture-dispatch-2026-11-19.md`, `physis-stage85-fracture-regression-2026-11-19.md`.


## Stage 87 — host-owned PHYSIS -> TerraWeave / Box3D fall bridge

- New optional authoritative host ledger and ECS component binding live in `engine/world/PhysisNatureFallHost.h`, **not** inside `engine/physis`. Host resource `world.physis_nature_fall` v1 is opt-in during `GameModule::setup`; existing game save manifests remain unchanged unless explicitly enabled.
- `engine/world/PhysisTerraWeaveFallBridge.h` is the actual typed adapter; it includes canonical TerraWeave root state and Box3D FallenPlantPhysics (no duplicate solver). The separate active TerraWeave branch is **not** merged/copied; this header is only compiled by the conditional cross-system CTest once both branches coexist in a single integration checkout.
- Scheduling contract: PHYSIS force + contact -> nature stress -> fracture outbox; TerraWeave authoritative terrain/root updates -> `dispatchPhysisTerraWeaveFalls` at trusted host barrier; native World snapshots/saves include fall ledger; Box3D reconciler after TerraWeave scene binding -> physics step. Float Box3D is derived; mid-fall pose checkpoint/replay is still a separate host responsibility.
- Host ledger records exact generation-safe PHYSIS event identity + stable TerraWeave `speciesId/plantKey`; replay returns DurablyCommitted only for equal immutable payload; repairs rearm PHYSIS, retire the host record after authoritative repair, then let the derived physics reconcile remove the body. Real grasses remain patch/cluster work, never per-blade Box3D.
- Stage87 CTest `physis_nature_fall_host` covers persisted host logic on PHYSIS-only; optional `physis_terraweave_fall_bridge` needs both Terra headers and enabled Box3D. **Tests not yet run on this new code**; previous 28/28 PASS belongs to prior branch.
- Read handoff `docs/agent-handoffs/physis-stage87-terraweave-fall-host-2026-11-19.md` before continuing; do not merge another agent's active branch into this isolated worktree automatically.
