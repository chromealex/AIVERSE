---
name: aiverse-evolve
description: Use AIVERSE EVOLVE for Long-term ecological region changes driven by PHYSIS and consumed through TerraWeave forest ecology.
---
# EVOLVE — AIVERSE skill

## When to activate
Long-term ecological region changes driven by PHYSIS and consumed through TerraWeave forest ecology.

Typical requests: fertility, contamination, forest growth, environmental aging, succession. Use this skill only for this mechanic; for multi-system orchestration see `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/EvolveEcs.h`, `libraries/include/aiverselib/world/EvolvePhysisBridge.h`.
- **Authoritative ECS:** `evolve::Region, Pressure, ClimateLink`.
- **Lifecycle:** `aiverse::lib::worldprocess::installSuite(sim,flags) with flags.evolve=true`; per-tick behavior: `evolve.physis_climate -> evolve.succession`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **CMake target:** no dedicated `AIVERSE::lib_*` target for this skill. This is an opt-in existing-native-module gameplay recipe (`runtime: none` in `skills/catalog.json`). The actual project links its established `AIVERSE::libraries` and any optional host owners (Box3D, Renderer3D, PHYSIS, TerraWeave) only when needed.
- **Studio-authored components:** `EvolveRegion, EvolvePressure`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/EvolvePhysisHost.h`, `EvolveTerraEcologyBridge.h`, `WorldProcessGameplayHost.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/EvolveEcs.h"
aiverse::lib::worldprocess::SuiteFlags flags;
flags.evolve=true;
aiverse::lib::worldprocess::installSuite(sim,flags);
// After real PHYSIS and EVOLVE entities spawn, the native host may use
// attachEvolveClimate(world,producer,region,physisCell,link).
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
PHYSIS owns weather, TerraWeave owns real ecology/tree population, NEREID water. Editor worldProcessGizmo.radiusMetres is ONLY a visual helper and does not set EvolvePlantRegion's actual rectangle.

## Acceptance checklist (execute when user lifts test deferral)
- Climate tick ordering, bounded rates/catchup, overlapping biome regions, no second weather authority, valid PHYSIS sources, explicit real host region bounds.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage13/23–28 native forest and editor verification deferred.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage13-granular-evolve-society-2026-11-23.md`, `docs/handoffs/world-processes-stage23-evolve-physis-terra-2026-11-23.md`, `docs/handoffs/world-processes-stage25-gameplay-host-2026-11-23.md`, `docs/handoffs/world-processes-stage28-interactive-studio-gizmos-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stage*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stage*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.

## Stage 30: authoritative region bounds, not visual-only gizmos

The real simulation footprint is `evolve::Area` in
`libraries/include/aiverselib/world/EvolveArea.h`.
It is a persistent `evolve.area` v1 ECS component with
`centerXcm, centerZcm, halfWidthCm, halfDepthCm, enabled`.
The centre is derived from the owning authored map entity's real
`pos` at `WorldProcessesMapHydration.h` setup; the half-extents
come from that entity's `EvolveArea` map component. The same entity
MUST also author `EvolveRegion`. Bounds are half-open rectangles
in centimetres, valid half-extents 50–50000 cm.

**Add in Studio:** Add Component -> `EvolveArea`, then set
`halfWidthCm`/`halfDepthCm`, or drag its X/Z bounds directly in
native Scene. These edits use existing native
`studio.worldProcessAreaSet`, one MapDoc Undo/Redo step.
The read-only Live Inspector shows the real ECS Area.

**Consume in TerraWeave:** `evolve::extents(world)` returns
generation-safe bounded rectangles from authoritative Area ECS.
The preferred native `makeEvolveForestClimateSampler(world,baseline)`
and `WorldProcessGameplayHost::forestClimate(world,baseline)`
use precisely these rectangles to modify the existing
`ForestEcologyClimateSampler`. TerraWeave retains the actual forest,
PHYSIS/NEREID the baseline, and EVOLVE the region state and extents.

Legacy `worldProcessGizmo.radiusMetres` remains a visual-only
circle on maps without an `EvolveArea` component. It does NOT
modify the actual TerraWeave ecology region; do not mistake it for
gameplay bounds. Prefer the typed Area from now on.

For overlapping handles, repeated LMB clicks in the same small
screen-space area cycle stable hit candidates. The active field label
shows component/key and ordinal; Escape cancels a drag without
modifying MapDoc.

**Status:** Stage30 authored source, not compiled or
hardware-accepted. The user's test deferral remains active.
See `docs/handoffs/world-processes-stage30-evolve-area-handle-cycling-2026-11-23.md`.

## Stage 31: bidirectional EVOLVE rectangle authoring

`StudioWorldProcessesEditing.h` exposes four symmetric EvolveArea edges:
`AreaWidth`, `AreaWidthNegative`, `AreaDepth`, `AreaDepthNegative`.
All four write the existing typed `halfWidthCm`/`halfDepthCm` fields
through `studio.worldProcessAreaSet` on mouse release; they never change
ECS Area center or map entity position. The opposite side moves equally.
Negative modes were appended to the mode enum to preserve existing values.
The current center is still the canonical authored map entity `pos`.
No build or runtime tests have been run for Stage31 (deferred by user).
See `docs/handoffs/world-processes-stage31-bidirectional-handles-2026-11-23.md`.

## Stage 32: authored EVOLVE center offsets

`EvolveArea` supports `offsetXcm` and `offsetZcm` relative to the owning map entity `pos`. Both default to zero; Stage30 maps missing both remain valid. Native hydration writes the sum to persisted `evolve::Area.centerXcm/centerZcm`; TerraWeave consumes ECS `evolve::extents(world)` unchanged. Studio has separate X/Z center drag handles and persists offset fields through `studio.worldProcessAreaSet` / MapDoc Undo. All four extent handles use the offset center, but still resize symmetrically. Asymmetric resize is not yet implemented. Source-only, no compile/runtime QA (deferred). See `docs/handoffs/world-processes-stage32-evolve-center-offsets-2026-11-23.md`.

## Stage 33: asymmetric EVOLVE edge resize

Four native Scene handles now keep the opposite edge fixed. Dragging an edge sends `edgeSide: positive|negative` and the desired edge coordinate in centimetres relative to the map entity through `studio.worldProcessAreaSet`. The session atomically derives the center offset and half-extent in one MapDoc Undo operation; direct field edits without edgeSide preserve their existing semantics. The ECS area schema remains unchanged. Even-centimetre spans are enforced so center and half-extents remain integral. Tests deferred; see `docs/handoffs/world-processes-stage33-asymmetric-area-resize-2026-11-23.md`.

## Stage 34: live EVOLVE rectangle preview

Native Studio Scene now projects the candidate EvolveArea rectangle during valid center/edge dragging via pure `worldprocess_edit::previewArea` and existing `worldprocess_gizmos::rectangle` overlay shapes. No MapDoc/ECS mutation occurs until release. Current host uses fixed-opposite-edge asymmetric resize; symmetric mode is helper-only, not yet exposed as a user control. Source-only, not compiled/tested. Handoff: `docs/handoffs/world-processes-stage34-evolve-live-preview-2026-11-23.md`.

## Stage 35: Scene resize modifier

Plain LMB edge drag keeps the opposite EVOLVE area edge fixed. Hold Shift during an active edge drag for symmetric resize about the authored center. The live candidate rectangle follows the current modifier and the drag label shows the mode; the host highlights active handles. On mouse-up, symmetric resize sends a typed half-extent to `studio.worldProcessAreaSet` while asymmetric resize sends `edgeSide`; both use one existing MapDoc Undo transaction. No ECS/forest duplication. No compilation or runtime QA performed (user deferred). See `docs/handoffs/world-processes-stage35-evolve-resize-modes-2026-11-23.md`.

## Stage 36: Scene snapping and numeric size preview

Hold Ctrl or Command while dragging EvolveArea center/edge handles to snap the local drag coordinate to a 1 metre grid. Shift still selects symmetric resize; plain drag keeps the opposite edge fixed. Native Scene overlay shows candidate full width/depth in centimetres and active mode. This is transient editor state; the only authored write remains `studio.worldProcessAreaSet` at release. Multi-selection, object snapping and editable numeric Scene fields are not implemented. Source-only; no native build or runtime tests yet. See `docs/handoffs/world-processes-stage36-evolve-grid-snap-dimensions-2026-11-23.md`.

## Stage 37: atomic EVOLVE multi-region center move

Native Scene center-handle drag on a multi-selection submits `studio.worldProcessAreaBatchMove` with stable `entityIds`, axis `x|z`, and integer `deltaCm`. The native session validates every selected EvolveRegion/EvolveArea and coordinate budget, then applies all center offsets with one MapDoc Undo entry. Mixed or locked selections fail atomically. Single-region edits retain the existing RPC. Group edge resize and group preview are not yet implemented. Source-only, no compilation/runtime QA. See `docs/handoffs/world-processes-stage37-evolve-batch-centers-2026-11-23.md`.

## Stage 38: EVOLVE neighbor boundary snapping

Ctrl/Command during native Scene EvolveArea edge drag now prioritizes the closest enabled neighboring authored EvolveArea ±X/±Z edge within 100 cm; stable map ID breaks distance ties. If no neighbor is close, the existing 1m grid snap applies. Center handles remain grid-only. The helper `worldprocess_edit::snapNeighborEdge` is read-only; only the owning area is edited on release through the existing MapDoc RPC. This is axis-only snapping, without perpendicular overlap filtering yet. Source-only, not compiled or runtime tested. See `docs/handoffs/world-processes-stage38-neighbor-edge-snap-2026-11-23.md`.

## Stage 39: physical neighbor snap and guide

`worldprocess_edit::snapNeighborEdge` requires positive overlap between the owner and neighbor intervals perpendicular to the dragged X/Z edge; corner-only contact cannot snap. Closest enabled EvolveArea boundary wins, ties resolved by stable map ID. The native Scene draws the overlap segment as a highlighted guide with target stable ID during Ctrl/Command snapping; otherwise 1m grid fallback applies. Drag state is transient and the authored change remains one MapDoc RPC. Source-only, uncompiled and untested. Handoff: `docs/handoffs/world-processes-stage39-overlap-snap-guide-2026-11-23.md`.

## Stage 40: atomic EVOLVE group resize

`studio.worldProcessAreaBatchResize` accepts stable `entityIds` (2–256), axis `x|z`, pivot `shared|individual`, and integer scale numerator/denominator. It validates all members and bounds before one MapDoc Undo mutation. In native Scene, multi-selected EvolveArea edge drag scales on one axis; plain drag uses the shared mean center pivot, Shift uses individual region centers. Single-region operations remain unchanged. Only authored offsets and half-extents change; ECS Area/TerraWeave remain authoritative at runtime. Group preview is not yet implemented. Source-only, no build or runtime tests. See `docs/handoffs/world-processes-stage40-group-resize-2026-11-23.md`.

## Stage 41: group resize preview

`worldprocess_edit::previewGroupResize` computes transient candidate rectangles for all selected EVOLVE areas during a group edge drag. Studio overlays amber outlines and a shared centroid marker; Shift uses individual pivots. The preview is read-only and the Stage40 atomic batch RPC remains the sole authoring commit. No editable numeric Scene input or custom pivot yet. No compile/runtime QA performed (deferred). Handoff: `docs/handoffs/world-processes-stage41-group-preview-2026-11-23.md`.

## Stage 42: two-axis group scale and custom pivot RPC

`studio.worldProcessAreaBatchResize` now accepts `axis:"both"`, independent X ratio (`scaleNumerator`,`scaleDenominator`) and Z ratio (`scaleNumeratorZ`,`scaleDenominatorZ`), plus `pivot:"custom"` with world-centimetre `pivotXcm`,`pivotZcm`. It stages all selected EvolveArea offset/half-extent changes and commits one MapDoc Undo after validation. Legacy one-axis/shared/individual calls remain. Native Scene numeric inputs and custom-pivot gizmos are **not yet implemented**; the new operation is accessible through the RPC. No build/runtime QA performed (deferred). See `docs/handoffs/world-processes-stage42-two-axis-custom-pivot-2026-11-23.md`.

## Stage 43: numeric 2D preview geometry and pivot overlay

`worldprocess_edit::previewGroupScale2D` computes read-only candidate rectangles for exact rational X/Z scale and a custom world-centimetre pivot. Native Scene now draws a numeric pivot label/marker and example group preview during drag. The Stage42 batch RPC still commits exact custom-pivot/two-axis edits. **Editable numeric Scene controls and draggable pivot gizmo are not yet implemented.** No compile or runtime QA performed (deferred). See `docs/handoffs/world-processes-stage43-two-axis-preview-pivot-2026-11-23.md`.

## Stage 44: draggable editor-only group pivot

Native Scene now exposes GroupPivotX/GroupPivotZ drag handles for multi-selected EVOLVE areas, derived from the group centroid. Dragged custom pivot is transient Studio state, reset on selection change; subsequent group resize passes `pivot:"custom"` and integer world-centimetre X/Z coordinates to the Stage42 atomic batch RPC. Numeric pivot labels are read-only. Editable numeric Scene controls, simultaneous two-axis drag and consolidated custom-pivot preview remain unfinished. Source-only, no build/runtime QA. Handoff: `docs/handoffs/world-processes-stage44-draggable-group-pivot-2026-11-23.md`.

## Stage 45: unified group preview

Native Scene now uses one group preview path for individual centers (Shift), shared centroid, and transient custom group pivot. It draws one group of candidate rectangles and one pivot marker; actual commits remain through `studio.worldProcessAreaBatchResize`. Editable numeric Studio Scene controls and a two-axis corner drag are still outstanding; exact numeric RPCs already exist. Source-only, no compile/runtime QA. Handoff: `docs/handoffs/world-processes-stage45-unified-group-preview-2026-11-23.md`.

## Stage 46: numeric group authoring UI

The HTML5 Studio shell now mounts `apps/studio-web/src/components/EvolveGroupNumeric.tsx` for multi-selection. It exposes exact integer X/Z scale percentages, pivot mode (shared, individual, custom) and optional world-centimetre pivot X/Z, calling `studio.worldProcessAreaBatchResize` with `axis:"both"` in one transaction. Native validates every selected EVOLVE region. UI is a compact overlay; numeric typing does not yet drive live Scene preview and no two-axis corner gizmo exists. No TS/C++ compilation or runtime QA performed (deferred). Handoff: `docs/handoffs/world-processes-stage46-numeric-studio-controls-2026-11-23.md`.

## Stage 47: two-axis group corner gizmo

For multi-selected EvolveArea regions, native Studio adds a +X/+Z corner drag handle (`GroupCornerXZ`) and stores X/Z drag coordinates separately. It renders transient candidate group rectangles and submits `studio.worldProcessAreaBatchResize` with `axis:"both"`, independent X/Z scale ratios and the selected shared/custom/individual pivot mode on release. Ctrl/Command snaps both coordinates to a 1m grid. Stage46 numeric input does not yet drive Scene preview while typing, and Shift individual-pivot corner preview needs parity work. Source-only; no compilation/runtime QA. Handoff: `docs/handoffs/world-processes-stage47-two-axis-corner-2026-11-23.md`.

## Stage 48: numeric-to-Scene preview bridge

Studio HTML5 numeric group scale panel now calls read-only host RPC `studio.worldProcessAreaPreview` while X/Z percentage or pivot inputs change, clearing it on invalid input/close. Native Scene renders transient cyan group rectangles using `previewGroupScale2D`; individual pivot mode is now supported by that helper and the Shift corner preview. Commit remains the Stage42 atomic batch resize RPC. Native draggable pivot is not yet synchronized back into HTML5 fields, and request ordering/compile/runtime QA remain pending. Handoff: `docs/handoffs/world-processes-stage48-numeric-preview-2026-11-23.md`.

## Stage 49: pivot synchronization and preview revision guards

Native host RPC `studio.worldProcessGroupPivot` returns current group pivot X/Z in world cm. Native Scene pivot drag emits `worldProcess.pivotChanged`; Studio HTML5 numeric controls subscribe and refresh their custom pivot fields. Read-only `studio.worldProcessAreaPreview` now requires monotonically increasing `revision` and `selectionKey`, rejecting stale/mismatched previews; React increments revisions for edits and clear requests. Pivot is transient editor state; actual region changes still use atomic batch RPC. Multi-window arbitration and exact preview/commit math remain outstanding. No TS/C++ compilation or runtime QA. Handoff: `docs/handoffs/world-processes-stage49-pivot-sync-revisions-2026-11-23.md`.

## Stage 50: shared scale math and preview ownership

`aiverselib/world/EvolveAreaScaleMath.h` provides pure integer-centimetre `evolve::scale_math::transform`, reused by numeric group preview and `studio.worldProcessAreaBatchResize` commit. Numeric custom pivot changes update the transient native Scene pivot marker. Numeric preview/clear requests now carry per-window owner tokens; native host applies per-owner revision checks and prevents inactive-window clear from erasing active preview. This is last-active-window ownership, not simultaneous multi-window compositing. No C++/TS compilation or runtime QA performed. Handoff: `docs/handoffs/world-processes-stage50-shared-math-preview-ownership-2026-11-23.md`.

## Stage 51: preview lifecycle and typed validation

Repaired the malformed Stage49 React numeric preview effect and ensured invalid input sends revisioned, owner-scoped clear requests. Native host now lets the active owner clear even after selection changes, rejects other owners' clears, and invalidates numeric preview on selection change. Numeric preview validates authored `EvolveArea` through `lib::worldprocess::extended::validate`, matching the Stage42 commit eligibility better. Multi-window lifecycle and full transaction parity still require work. Source-only; no TS/C++ build or runtime QA. Handoff: `docs/handoffs/world-processes-stage51-preview-lifecycle-validation-2026-11-23.md`.

## Stage 52: window lifecycle and shared bounds validation

Numeric EVOLVE preview now releases its owner-scoped native overlay on HTML5 pagehide/unmount and suspends while document visibility is hidden; visible windows refresh with revisioned updates. Shared `evolve::scale_math::withinBounds` validates world-coordinate half-extent constraints in both two-axis group preview and atomic batch commit, alongside Stage50 shared transform. Unfocused-but-visible floating windows and legacy single-edge parity remain to be addressed. Source-only, no TS/C++ compile or runtime QA. Handoff: `docs/handoffs/world-processes-stage52-window-lifecycle-bounds-2026-11-23.md`.

## Stage 53: single-area validation and native host shutdown

Single-area and group-move commits use shared `evolve::scale_math::withinBounds`; single-area edits reject hidden/locked entities. Single-area and one-axis group previews use authored limits and shared geometry checks. Native `StudioHost::shutdown` releases host-local numeric preview and pivot/drag state even without web pagehide. Cross-host forwarding and complete validation parity still require integration audit. No build or runtime tests performed. See `docs/handoffs/world-processes-stage53-single-area-native-shutdown-2026-11-23.md`.

## Stage 54: typed Studio bridge audit
EVOLVE preview, pivot and batch resize RPCs are registered in the HTML5 typed bridge; browser mock rejects native-only authoring. No build or tests. See `docs/handoffs/world-processes-stage54-typed-bridge-audit-2026-11-23.md`.

## Stage 55: hydration and native RPC audit
Map hydration rounds Fixed positions to nearest cm instead of truncating; numeric preview rejects locked/hidden entities and invalid custom pivot coordinates. TerraWeave consumer audit and runtime parity remain pending. No builds/tests. See `docs/handoffs/world-processes-stage55-hydration-rpc-audit-2026-11-23.md`.

## Stage 56: TerraWeave consumer audit
Canonical forest climate sampler rechecks live ECS Area and Region and current half-open bounds before applying climate. Rebuild sampler each ecology step to include newly added/enabled regions. Baseline and forest ownership unchanged. Production call-site audit and QA remain pending. See `docs/handoffs/world-processes-stage56-terraweave-consumer-2026-11-23.md`.

## Stage 57: synchronous ecology adapter
`WorldProcessGameplayHost::advanceEcology` builds a fresh ECS climate sampler per synchronous TerraWeave ecology call. Caller owns forest/roots and must restore both to the same completed tick after rollback. Production tick wiring and QA pending. See `docs/handoffs/world-processes-stage57-ecology-step-2026-11-23.md`.

## Stage 58: paired ecology checkpoint
`WorldProcessGameplayHost::captureEcology` and `restoreEcology` provide caller-owned in-memory checkpoints of forest resource and rooted plants, guarded by completed tick. Automatic production tick and disk persistence wiring remain pending. No tests. See `docs/handoffs/world-processes-stage58-ecology-checkpoint-2026-11-23.md`.

## Stage 59: authoritative persistent ecology
Prefer `WorldProcessGameplayHost::advanceEcologyEcs` / `addEcologyEcsSystem` over standalone checkpoints. Delegates to existing TerraWeave World COW resources and persistence; register after EVOLVE and terrain/water prerequisites, not alongside a second forest update. Production registration and QA pending. See `docs/handoffs/world-processes-stage59-persistent-ecology-ecs-2026-11-23.md`.

## Stage 60: TerraWeave registration opt-in
`terraweave_native::registerSystems` accepts trailing `enableEvolveForestClimate=false`; when enabled with authored forest species, replaces the normal forest tick with EVOLVE-aware ECS update, retaining persistence and callbacks. Register PHYSIS/EVOLVE first. Do not register a second forest system. Source-only, no tests. See `docs/handoffs/world-processes-stage60-forest-registration-2026-11-23.md`.

## Stage 61: dependency preflight
EVOLVE-aware TerraWeave registration requires nonempty species, exactly one preceding `evolve.succession` and no prior `terraweave.forest_ecology`. README documents composition. PHYSIS order is caller responsibility; standalone demo and QA pending. See `docs/handoffs/world-processes-stage61-dependency-preflight-2026-11-23.md`.

## Stage 62: forest demo fixture
`examples/native/world_processes/evolve-forest-demo.json` provides healthy, polluted and recovery regions with typed areas and pressure sources. Concrete game must supply entity types, plant population and real PHYSIS links; not a runnable demo. No tests. See `docs/handoffs/world-processes-stage62-forest-demo-fixture-2026-11-23.md`.

## Stage 63: native demo setup
`EvolveForestDemo.h` stages real PHYSIS climate links and caller-authored rooted plants in existing World authority. Caller supplies resolved entities and anchors. No executable demo or tests yet. See `docs/handoffs/world-processes-stage63-demo-setup-2026-11-23.md`.

## Stage 64: headless demo target
`aiverse_evolve_forest_demo` CMake target hydrates the demo fixture, plants 27 trees through TerraWeave authority and advances ECS ecology. Source only; PHYSIS/NEREID setup, disk CLI and QA pending. See `docs/handoffs/world-processes-stage64-headless-demo-2026-11-23.md`.

## Stage 65: demo weather, water and replay
Headless demo now initializes PHYSIS grid, EVOLVE climate links and NEREID lake; optional `--replay` restores a Simulation snapshot and compares checksum. Disk save/load awaits PHYSIS persistence audit. No build/run. See `docs/handoffs/world-processes-stage65-demo-weather-water-replay-2026-11-23.md`.

## Stage 66: demo persistence
Native PHYSIS install registers persistence; demo supports `--load FILE`, `--save FILE`, `--replay` and forest diagnostics. No build or roundtrip verification. See `docs/handoffs/world-processes-stage66-demo-persistence-2026-11-23.md`.

## Stage 67: SVG ecology report
Demo `--svg FILE` exports ECS region fertility/pollution and TerraWeave live/dead/fallen plant markers. Fixed demo viewport, no live renderer. No build/run. See `docs/handoffs/world-processes-stage67-svg-report-2026-11-23.md`.

## Stage 68: Studio ecology layers
`studio.evolveLayer` controls authored Scene colors; `studio.evolveState` supplies live ECS minimap regions, rooted/fallen vegetation and PHYSIS weather. HTML5 controls visibility and opacity. No runtime verification. See `docs/handoffs/world-processes-stage68-studio-ecology-2026-11-23.md`.

## Stage 69: source audit and acceptance
`tools/evolve-demo-acceptance.py` checks rollback, disk roundtrip, continuation and SVG after native build. No execution yet. See `docs/handoffs/world-processes-stage69-source-audit-2026-11-24.md`.

### Studio ecology stages 71–75
Use studio.evolveState for read-only live region, plant, deadwood and soil diagnostics. History is editor-local, bounded to 240 samples. Presets use EvolvePresets.h and Inspector setProperties; custom presets currently localStorage plus JSON exchange. Time controls reuse studio.gameControl pause/resume/step for the whole local match, not a separate ecology clock. No builds/tests run yet.

## Stage 77: tick-scoped legacy ecology index
`WorldProcessGameplayHost::advanceEcology` now uses `makeIndexedEvolveForestClimateSampler` with the shared `Bvh2D` and precomputed sorted ECS extents. Rebuild per synchronous step; Area bounds and membership must remain fixed during sampling, including baseline callbacks. The public reusable `forestClimate` sampler preserves live-area semantics. Scratch is per callable, not concurrent. No persistence schema or climate formula change; build and parity acceptance pending. See docs/handoffs/world-processes-stage77-legacy-bvh-2026-11-24.md.

## Stage 78: shared Simulation LOD and lazy forest BVH
Use the canonical `engine/world/SimulationLod.h` (same blob as simulation cadence branch); do not invent another scheduler. `EvolveSimulationLod.h` exposes read-only `planRegionWork` and `regionWorkDecision` for optional advisory regional work. Observer cells are authoritative and must share the caller-selected cell size in cm; distance is planar to nearest rectangle cell, not center. Missing/invalid bounds, observers, config or cell size fail hot; critical owner predicate forces hot. Stable Entity ID/tick determines due phase. Diagnostics executed means planned due, not completed callback. Do not gate pressure integration, climate, death/recruitment, physics, or persistent ecology by this plan; exact historical rates and ordering would be lost. Forest ECS host builds its BVH lazily inside first climate callback; no index on non-sampling ticks. No archive changes. Linked test source added, not run. See docs/handoffs/world-processes-stage78-simulation-lod-2026-11-24.md.

## Stages 79–80: budgeted advisory region work
`EvolveRegionWorkBudget.h` uses existing `aiverselib/runtime/WorkBudget.h::runBudget` (no fork). `prepareRegionBudget(plan,state,cap)` is pure: return selected work and next state, commit together with staged owner outputs on success. One call per strictly increasing tick, explicit zero cap pauses service. Sorted generation-safe identities, coalesced pending requests, stable last-served cursor, removal of inactive regions, retained first due tick; cap counts bounded visits only. `prepareRegionReports` is a read-only concrete consumer returning Region copies with requestedTick/sampleTick; caller owns cache and checkpoint state. No automatic persistence/Studio wiring; never drive authoritative integrators from stale samples. See stage79 and stage80 handoffs. Test source added, not executed.

## Stage 81: bounded wake requests
Use `RegionBudgetOptions{maxVisitsPerTick, wakeSpan}` with prepareRegionBudget/prepareRegionReports. Wake requests bypass the LOD due phase, not the operation cap. Duplicate IDs coalesce, inactive IDs are counted and ignored, invalid IDs reject. Pending urgent work retains firstDueTick until served; ordinary and urgent lanes each use shared WorkBudget and independent stable-ID cursors. Contested slots alternate across ticks, even cap=1. No strict latency guarantee under overload or churn. Persist/copy all RegionBudgetState fields with owner state if gameplay uses the plan; no automatic archive registration. A wake is an advisory refresh, not a lifecycle event. No build/test execution. See docs/handoffs/world-processes-stage81-priority-wake-2026-11-24.md.

Stage 82: EVOLVE's existing RegionBudgetState/Batch/Options and prepareRegionBudget remain source-compatible through the shared `WorldProcessEntityBudget.h`. Do not copy the queue algorithm. CROWD and SOCIETY use independent owner-scoped EntityBudgetState instances. See stage82 handoff.

## Stage 91: planetary EVOLVE coverage
`EvolveSurfaceRegions.h` supplies `SurfaceArea` (canonical origin/body plus existing Area dimensions and local east/south offsets in cm), `setSurfaceArea`, `SurfaceRegionIndex`, and seam-independent azimuthal equidistant coverage using existing sphereArcTurns/sphereTangentToward. Rectangles have half-open bounds, shortest arc distances, deterministic antipodal chart cut; use multiple regions for global coverage. Query index is body-bucketed, sorted by packed Entity, rebuilt per sampling barrier (no persistent duplicate area/cache), reads Region values live. Native GameCore planet maps hydrate existing authored EvolveArea from actual Transform resolved via at/body/face; register evolve.surface_area instead of flat evolve.area. Do not mix both area types. Plane behavior unchanged. Existing Region/Pressure/ClimateLink and PHYSIS producers unchanged. `EvolveSurfaceEcology.h::sampleSurfaceEcology` applies shared `projectEvolutionClimate` to explicit 3D plant/sample position and existing TerraWeave baseline; never project a legacy planar anchor to a sphere. Planet plant owner integration and body-aware Studio/LOD visualization still pending. Tests authored, not run; see Stage91 handoff.

## Stage 99: body-aware planetary ecology sampling

`EvolvePlanetEcologySampling.h::samplePlanetEcology` takes up to 4096 owner-provided ground sites per synchronous batch, with stable body ID and existing TerraWeave ForestEcologyClimate baseline. It validates Surface contact and body, builds one existing SurfaceRegionIndex, and projects canonical Region fertility/pollution/succession into the existing climate type. Returns body/face/cell, contributing region count and optional PHYSIS observations (temperature, humidity, rain, wetness, snowpack, ice). Explicit host-mapped weather source must be on the same body and within the configured chord-distance limit; no implicit mapping from planar PHYSIS grid. Optional surface-wetness cap is an explicit gameplay approximation, not soil-water physics. Invalid inputs fail the entire read-only batch. No cache survives rollback, no new plant state or persistent schema. Inputs must remain stable for the synchronous owner barrier.

PlanetFlora is still cosmetic; authoritative TerraWeave ForestEcologyEcs currently uses planar roots, canopy and seed offsets. Real planetary growth, reproduction, mortality, persistent plant positions and render publication require the TerraWeave owner to generalize that lifecycle. Do not run planar ecology on cube-face coordinates or duplicate the plant registry. Native tests and authored demo sample assertions added; NOT RUN. See Stage99 handoff.

## Stage 104: spherical EVOLVE direct editing

Select one authored EVOLVE region on a planet and use the five visible Scene handles (four edges, center). The native viewport raycasts its actual coarse terrain and projects to the existing EVOLVE chart. Default edge drag fixes the opposite edge; Shift resizes symmetrically; Ctrl/Cmd snaps chart coordinates to one metre. Center moves both offsets. Yellow preview stays transient, Escape cancels. On release a single existing studio.worldProcessAreaSet request with key bounds, four integer centimetre fields and expectedRevision commits through MapDoc. Invalid, stale, hidden/locked and antipodal preview candidates are rejected; no second document or simulation. Planar editing remains unchanged. No neighboring-region snap or group handles on spheres.

Corrected Stage102/103 planet picking/cache identity: UI live MapDoc and render snapshots are distinct objects; use path/revision/body/project context instead of pointer equality, do not retain a raw snapshot pointer for drag. Periodic snapshot renewal no longer forces camera reset merely due to pointer changes.

Source fixtures cover chart roundtrip, edge anchoring, symmetric resize, snap, complete bounds transaction, stale revision and Undo. Native tests/build NOT RUN. PHYSIS graphical handles, rotation/scale editing, live overlays and integration remain open.

## Stage 108: live planetary EVOLVE regions

With local Play current map, a saved unchanged document, Live 3D overlay and gizmos, the Scene draws lifted read-only outlines of the actual ECS Region + SurfaceArea on the selected body. The existing EVOLVE layer selector chooses combined fertility/pollution, fertility, pollution, succession or durability colors. Authored outlines and authoring handles remain separate. No browser-side ecology or planar projection of spherical regions.

The shared `evolve::surfaceRegionDirection` in EvolveSurfaceRegions.h is the inverse of the existing azimuthal chart on its open domain, solving the nonorthogonal face-axis Gram matrix and rejecting the antipodal cut. Existing authored preview and handles use the same inverse via their forwarding function. The live path projects each sampled direction onto actual canonical Surface terrain, subtracts the body anchor and lifts the outline. Stable packed Entity ordering; maximum 64 regions globally, 4 edges × 16 segments per region; invalid, stale, disabled or ambiguous owner states are skipped; invalid chart points break the polyline, never bridge the cut. This is a sampled boundary, not exact filled global coverage or a planetary climate/vegetation simulation. Snapshot reports actual ecological values, completed tick, capped/rejected/clipped counts, body ID and region ID. HTML5 panel summarizes the first 12 sampled regions and the total; existing layer and tick controls are reused.

Existing physical/ecology state, save schemas and planar authoring are unchanged. Source fixtures cover inverse roundtrip, moon body identity, values, stale sample, disabled area, cap and antipodal clipping. Build, native tests, browser typecheck, interactive UI, screenshots and GPU performance NOT RUN. Next: PHYSIS effective live gravity visualization from the authoritative gravity sampler; TerraWeave planetary ecology lifecycle and terrain mutation remain owner dependencies.

## Stage 122: EVOLVE planetary root climate routing

`EvolvePlanetRootClimate.h` provides a native tick-scoped batch for existing rooted vegetation owners. Explicit canonical ground mappings must reference actual PHYSIS State grid cells at the requested climate tick. Deterministic nearest in-coverage same-body source selection crosses cube-face seams; no cross-body lookup or implicit planar grid projection. Existing EVOLVE climate sampling limits the owner baseline; optional uncovered sites retain baseline weather inputs, required uncovered sites fail the batch. Bounded comparisons, no new persistent state. Actual spherical rooted flora lifecycle and automatic host/renderer integration still depend on TerraWeave. Fixture source added, tests NOT RUN. See `docs/handoffs/world-processes-stage122-planet-root-climate-2026-11-24.md`.

## Stage 156: planetary natural mortality fall

Optional native `planetForest.naturalFallPermille` (0 disabled, 1..1000 remaining organic mass threshold) turns standing dead rooted trees/shrubs into the existing fall outbox after growth/decay and before nutrient cycling. Actual PHYSIS acceleration supplies tangent direction where nonzero; radial/zero gravity uses the existing stable root azimuth. No new mass ledger, soil edit or physics solver. Optional per-species `naturalFallBody` with radiusCm, halfHeightCm, density, frictionPermille creates an explicitly authored durable capsule and acknowledges the fall atomically. Otherwise the existing native consequence owner must consume the pending receipt. The native PlanetProcessHost projects durable records into Box3D, applies PHYSIS and captures checkpoints; GameCore does not automatically own a Box3D host. Fully spent wood does not create new bodies. See Stage 156 handoff. Build/CTest/physical acceptance NOT RUN.

## Stage 157: spent planetary forest physical retirement

`retireSpentPlanetForestBodies` runs in the existing due forest batch before new natural falls. It removes only plant consequence records with zero actual remaining organic mass, matching the fallen root, historic cell/body and exact acknowledged receipt. It preserves the receipt, deadwood tombstone, nutrient balance and all structure consequences. The native PlanetConsequencePublisher reconciles the reduced set on its next physics publication; rollback resets native handles and reconstructs from the restored records. Missing ledger data is not zero. Pending undelivered falls and standing dead static colliders remain outside this stage. Source regression added; build/CTest and real physics acceptance NOT RUN.

## Stage 159: ARBOR trunk-derived planetary collision

Optional per-species `naturalFallBody: {"fromArbor":true,"density":600,"frictionPermille":500}` resolves the exact matching map `world.terraweaveForest.species` profile and seed through a shared CPU-only parser. The generated structural trunk determines capsule height and radius, scaled by the existing age/vigor, shrub and body flora scale policies. Remaining deadwood mass scales the radius by square root at delivery; the shape is frozen after creation. The renderer uses the capsule's root-pivot offset. Explicit radiusCm/halfHeightCm mode remains supported, mutually exclusive with fromArbor. This is a structural trunk approximation, not compound branch collision or continuous resizing. Native fixture source added; NOT RUN. In Studio Play, a fallen plant with this binding uses the seeded species bark and branch mesh. Its base and tip follow the published capsule pose and its thickness follows the remaining trunk radius. An explicit radius/half-height recipe displays a cylindrical log.
