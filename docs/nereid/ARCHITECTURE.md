# AIVERSE NEREID — architecture and integration contract

NEREID is the universal, genre-independent water framework of AIVERSE. It is
**not** a fork of RTS water nor a renderer-only material. Existing legacy
water remains active until explicit, tested migration.

## Existing water (do not remove)

- `modules/rts/view/RtsWater.{h,cpp}`: spline networks, shores, geometry, water cells.
- `modules/rts/view/RtsShoreView.{h,cpp}`: shores, props and foam rendering.
- `apps/editor/core/WaterOps.{h,cpp}`: editor bake and water operations.
- `apps/editor/windows/WaterWindow.cpp`: water editing UI.
- `tests/tests_water.inc`: existing water/shore/map behavior.
- `docs/water.md`: existing scene/map representation.

The previous `waterNetwork` is baked into map tiles and the RTS simulation
consumes these tiles, **not** the source water body splines. Do not change
baked tile semantics, game saves, terrain generation or shore placement by
introducing NEREID.

## Stage 1 — authoritative gameplay data + sampling

Public C++ headers:
- `<aiverselib/water/NereidWater.h>`
- `<aiverselib/water/NereidWaterSystems.h>`
- Package: `AIVERSE::lib_nereid_water` from `libraries/catalog.json`.

Namespace: `aiverse::lib::nereid`.

ECS component contract:
- `WaterBodyComponent`: authoritative definition, fixed-point coordinates
  (X/Z horizontal, Y up), kind (Ocean/Lake/River/Pool/Puddle), footprint
  (Infinite/Box/Disc), surface/bottom Y, flow, overlap priority, enabled.
- `WaterProbeComponent`: authoritative query point provided by host input,
  physics, transform sync or game-specific policy.
- `WaterContactComponent`: ECS-owned sampled result and tick; not kept as
  independent FPS/RTS vector state.
- `WaterSample`: selected generation-safe body Entity, kind, surface/bottom,
  clamped immersion depth, flow, priority and found/submerged bits.

Only point-in-footprint sampling is implemented at this stage. A water
`kind` does not automatically supply a spline, hydrodynamics or waves.

## Selection / gameplay ordering

1. Host input/movement/physics writes `Transform` or `WaterProbeComponent`.
2. Optional `addWaterProbeFromTransformSystem(sim,name)` copies the shared
   `Transform::pos` into probes; it is not needed for custom probe producers.
3. `addWaterSamplingSystem(sim,name)` scans water bodies and updates
   `WaterContactComponent`. Higher priority wins; same priority takes higher
   surface, then lowest packed Entity ID. Query does not depend on pool order.
4. Game-specific swim, buoyancy, currents and damage stages consume contact.
5. Renderer may consume presentation snapshots and add GPU-only detail.

This is O(body count * probe count); spatial acceleration is planned.
No bodies or probes are structurally changed during the sampling pass.

## Determinism, persistence and native code

- Authoritative values use `aiverse::Fixed` / integer, never floats or GPU
  wave samples. No RNG or genre branches in NEREID core.
- Register `registerNereidWaterPersistence(sim)` during the host module's
  `setup`, before stepping/sealing, to add versioned native ECS schemas:
  `nereid.water_body`, `nereid.water_probe`, `nereid.water_contact`.
- World-native snapshots, hashes and serialization are host-integration
  concerns: do not add a second mutable mirror. The host must also persist
  any independent Transform/movement state affecting future probes.
- Add water to an arbitrary native game's public target by linking
  `AIVERSE::lib_nereid_water`. Do not link FPS/RTS for the library.
- `tests/ecs_nereid_water_test.cpp` exercises two unrelated hosts, rollback,
  COW, native save/load and checksum for NEREID components. These are
  authored tests, **not yet executed** in the current implementation.

## Roadmap and constraints

Stage 2: shared water-body geometry adapters (legacy `waterNetwork`,
spline river, closed lake polygon), explicit world position/terrain height
contract, accelerated spatial queries; test old water editor/bake parity.
Stage 3: render backend with low/medium/high profiles, depth/shore foam,
SSR/planar reflection fallback, LOD and underwater view.
Stage 4: composable water mechanics (buoyancy, swimming, drag, currents, wakes,
impulses, waterfalls), deterministic CPU game state + GPU visual layering.
Stage 5: editor UX, auto-shores, weather coupling, serialization upgrades,
native and existing FPS/RTS adoption, platform performance/CI.

Do not claim stage 3/4 features exist because corresponding `WaterKind`
values are present in the enum.

## Stage 2 — cross-genre buoyancy output

`<aiverselib/water/NereidBuoyancySystems.h>` exposes the optional
`AIVERSE::lib_nereid_buoyancy` package. It runs only when Entities have
WaterBuoyancyComponent, WaterVelocityInputComponent, WaterContactComponent
and WaterForceComponent.

`computeWaterForces` uses fixed-point immersion fraction =
clamp(contact.depth / immersionHeight, 0, 1). Output water acceleration =
up * buoyancyAcceleration * fraction -
(velocity - waterFlow) * linearDrag * fraction.
This excludes ordinary gravity and does not integrate movement or
alter contacts. A host's canonical physics owns all velocity and spatial
updates. Stage order must sample water BEFORE buoyancy and integrate AFTER.
Missing component or sample from another tick => zero output.

WaterVelocityInput is a per-tick view of host canonical physics state; if the
host retains velocity in an existing ECS component, copy it once immediately
before buoyancy rather than inventing a persistent second velocity owner.
Register `registerNereidBuoyancyPersistence` in setup when enabled.
`ecs_nereid_buoyancy_test` is authored and not yet run.


## Stage 3 — ECS geometry override (implemented, unverified)

`NereidGeometry.h` defines `WaterGeometryComponent` as an optional
per-body footprint override: a closed simple polygon (lakes, pools), or
a variable-half-width X/Z polyline (rivers, canals). Geometry has a
maximum of 256 nodes for bounded ECS save and replay. Polygon banks and
spline bank edges are included in queries. Self-intersecting polygons and
holes are not a supported contract.

The shared `sampleWater` queries the optional component on each water
body Entity. If the component is absent or disabled, original v1
Infinite/Box/Disc footprints continue to work. An invalid enabled geometry
covers nothing; it cannot silently expand to a larger fallback. Body height,
flow, priorities and stable ID tie-breaks are unchanged. Geometry is
independent from RTS/editor `waterNetwork`; no implicit migration.

To opt in: call `registerNereidGeometryPersistence(sim)` in native
setup before the registry seals, and attach `WaterGeometryComponent`
to `WaterBodyComponent` entities. This adds the separate
`nereid.geometry` v1 schema, leaving the water-body v1 wire format alone.
`AIVERSE::lib_nereid_water` is still the native link target.

This is a pure deterministic CPU footprint policy. It has no spatial index
or geometry mesh generation, waves, shoreline features, VFX or editor
adapter. Geometry component storage, snapshot and rollback are ECS owned.
Test: `ecs_nereid_geometry_test` (authored but NOT RUN).


## Stage 4 — transient water spatial broadphase (authored, not built)

`NereidQueryIndex.h` provides an optional acceleration adapter. It builds
a deterministic AABB hierarchy from `WaterBodyComponent` and optional
`WaterGeometryComponent` data and samples it with the **same**
`considerWaterCandidate` selection as the reference `sampleWater`.
Finite Box/Disc/Polygon/RiverSpline volumes are indexed by conservative
horizontal bounds. Infinite oceans stay outside the tree. Both check
`coversWaterBody` before yielding a sampled contact. Bodies with invalid
enabled geometry are excluded rather than falling back to box/ocean.

Register **either** `addIndexedWaterSamplingSystem` **or**
`addWaterSamplingSystem`; both consume a probe and overwrite
`WaterContactComponent` with the same tick. The index is constructed on
each simulation tick and discarded before the tick ends. It stores a
reference to the World and must not be reused after World writes,
structural changes, restoring a snapshot or loading a native save.
Nothing about the indexed data itself is authoritative or persistent.

No changes to NEREID persistence schemas, renderer, RTS/editor waterNetwork
or existing save streams. Two-host indexed/reference parity test:
`ecs_nereid_query_index_test` (author-written, NOT RUN yet).

## Stage 5 — read-only adoption of legacy baked-water raster

The legacy `waterNetwork` stores editor-side Catmull-Rom splines
and sea shorelines in **cells**, with variable river width in metres.
Its `waterCells` function adds a 0.75-cell edge tolerance, interpolates
sea frontiers and supports headland/shore features. Consequently,
interpreting its original control points directly as NEREID straight
spline segments **would not preserve existing wet cells**.

Instead, `NereidRasterImport.h` implements a deterministic pure
converter from a generic binary raster to disjoint
`WaterBodyComponent` rectangles. Row-run compression also merges
identical spans in adjacent rows. This represents the baked water mask
exactly **at cell centres**, not an equivalent continuous shoreline.
Coordinates are X/Z world metres, `cellMeters` is explicitly Fixed,
and body heights are provided by the caller. There is no change to
existing NEREID schemas.

`modules/rts/view/NereidLegacyWaterAdapter.h` is the only
legacy-dependent header:
- `importWaterNetworkToNereid` uses the untouched legacy
  `buildGeometry` + `waterCells` rasterizer, and is appropriate for
  map source previews/baking.
- `importBakedWaterCellsToNereid` consumes the actual canonical game
  map cells (`~` by default), including post-bake changes; **prefer
  this for native gameplay migration**.

The adapter returns data without altering the input map or automatically
registering a game system. A native host opts in by creating entities,
registering `registerNereidWaterPersistence`, adding probes and
choosing one contact-sampling stage. No old RTS/editor save protocol,
shore/bank geometry or navigation is migrated automatically.
Baked cell masks lose per-body sea/river provenance, so callers specify
a common `WaterKind` and an explicit water surface/depth. To support
multiple semantic water levels, source distinct masks explicitly.

Fixture: `ecs_nereid_legacy_raster_test` (authored, NOT RUN)
compares all centres of an 80x64 legacy river/sea/strait map against
both NEREID sampling policies and verifies native save/COW/rollback.

## Stage 6 — native pipeline and adaptive sampling

The new `NereidAdaptiveWater.h` is an optional **deterministic**
selection policy. `addAdaptiveWaterSamplingSystem` evaluates ECS
water-body and contact-pool counts each tick; it uses reference scanning
below `minWaterBodies` or `minProbes`, or the transient stage-4 BVH
otherwise. This choice is not driven by elapsed time or presentation
frame rate, and both paths write identical `WaterContactComponent`
data under the same overlap/tick semantics. It introduces no persistence
schema. Only one contact-sampling system may be scheduled.

`NereidNativeWater.h` composes the existing reusable components and
systems from any native `GameModule::setup` without RTS/FPS/renderer
dependencies: `registerNativeWaterPipeline(sim,prefix,cfg)`.
Optional switches enable geometry persistence, buoyancy persistence and
force calculation, and transform-probe synchronization; the adaptive
sampler is the one contact stage. Physics integration remains the host's
responsibility. `spawnNativeWaterBodies(world,bodies)` creates water
body Entities deterministically; it deliberately does not invent an
independently writable water map. Attach probe/contact to any Entity
irrespective of genre.

CTest `ecs_nereid_native_pipeline_test` checks two unrelated hosts,
forced linear/indexed equivalent results, native save/load and COW
rollback. The ECS experimental shell runner now also compiles and
executes all six NEREID tests, including the legacy raster bridge.
**All results remain unverified until CI actually runs.**

## Stage 7A — performance characterization

`tests/ecs_nereid_sampling_bench.cpp` is an optional measurement
program (not a correctness-gated CTest). It uses `steady_clock` only in
the diagnostic process, never within simulation. It times a 3x3 matrix
of body and probe counts, including the **per-tick rebuild** of the
transient stage-4 AABB tree, and compares the resulting deterministic
sample signatures with the linear reference. Invoke CMake target
`aiverse_ecs_nereid_sampling_bench` or
`tests/run_ecs_experiment_suite.sh --bench`. Results are NOT YET
available; default adaptive thresholds remain heuristics.

## Stage 7B — reusable presentation geometry, not gameplay authority

`<aiverselib/water/NereidWaterSurfaceMesh.h>` provides independent
CPU mesh creation for NEREID water bodies using only generic ECS water
data: rectangles/discs, bounded infinite-ocean patches, simple concave
polygons and variable-width river ribbons. LOD controls tessellation
rather than entity/genre semantics. UVs and shoreline proximity
(`WaterSurfaceVertex.shore`) are outputs for rendering only.
`WaterSurfaceStyle` describes tint/foam/specular/alpha, without
imposing a backend. Invalid shapes are rejected rather than silently
rendered with an alternate footprint.

This is a read-only **presentation projection** of authoritative
`WaterBodyComponent`/`WaterGeometryComponent`, never persisted, hashed,
networked or read back into sim. Existing RTS/editor water surfaces,
shader effects, baked cells and shoreline models remain untouched.
New test `ecs_nereid_surface_mesh_test` was authored but NOT RUN.

## Stage 7C — renderer opt-in bridge and material identity

`engine/render/NereidWaterRenderer.h` is a small adapter from the
stage-7B pure `WaterSurfaceMesh` to the existing RHI-independent
`Renderer3D::MeshBuilder` and scene queue. It uploads vertices
with fixed upward normals and per-vertex foam/deep tint, draws a
translucent highly specular surface, and provides a matching
`releaseNereidWaterSurface` GPU lifetime helper. Nothing auto-
instantiates or caches an ECS water entity: scene/game owns upload
and draw. Geometry/LOD needs explicit rebuild when it changes.

The new `Renderer3D::kDrawWaterSurface` flag (256) is encoded in
existing draw U0 params.x as a **negative sentinel**. GLSL/MSL
lit shaders recognize it to run the *existing* animated open-water
normal shader at any water surface elevation, plus SSR where
trace-tier inputs exist, without misclassifying unrelated shiny
meshes. Mixing with the unlit channel clamps the sentinel to
0..1; old lit/unlit/debug modes remain unchanged when unflagged.
No extra GPU uniform block, texture slot or genre-specific
renderer is introduced.

**Known GPU limitation:** the existing planar reflection renderer
selects a single water plane per pass. Multiple simultaneous
NEREID surface levels need a future multi-plane solution or
selective SSR/environment fallback. No actual hardware rendering,
shader cross-compilation or C++ tests have yet been executed in
this stage.

## Stage 8 — per-surface planar reflection safety

`Renderer3DReflect.cpp` renders at most one planar-water reflection
per frame and records its selected world-space Y only when the render
target successfully begins. `Renderer3D` resets that derived state
each scene. The pure `NereidWaterReflectionPolicy.h` helper compares
world Y using a finite, 0.025-unit tolerance.

The opt-in `kDrawWaterSurface` flag now maps to existing U0.params.x:
`-1` for water using the selected planar reflection, `-2` for
water at another Y when a reflection pass exists. Both select the
existing open-water six-train wave shader and remain eligible for
trace-tier SSR; GLSL and MSL suppress only the invalid planar
`waterReflect` lookup for `-2`. Other materials and old unflagged
water behavior remain unchanged. The reflection-plane flag is
**presentation-only derived state**, never a WaterBody ECS source.
Multiple simultaneous planar reflection passes are still unsupported;
secondary surfaces fall back to SSR (when available) or lighting.


## Stage 39 — optional rapid foam palette

Per-water-surface presentation style now has `rapidFoamR/G/B` (normalized, default 0.84/0.93/0.94), read from scene JSON key `style.rapidFoamColor: [r,g,b]`. The value travels through the existing NEREID draw path to a new shared U0 `waterRapidColor` float4, at the end of the water uniform sequence in C++, GLSL and Metal. This only colours Stage 38 current-driven rapid foam when enabled; no ECS schema, water flow physics, additional GPU texture or pass is involved. Reflection uniforms use the same extended U0 layout. Built/shader-compiled validation not yet performed; see `docs/nereid/stage-39-rapid-foam-color.md`.


## Stage 40 — quality policy for current-driven whitewater

`WaterSurfaceStyle::rapidDetail` uses `WaterRapidDetail::Low/Medium/High` (Medium default), configured by scene JSON `style.rapidDetail: "low"|"medium"|"high"`. It is presentation-only and encoded in the previously unused alpha of `waterRapidColor`. Low uses fewer waves and no whitewater-normal detail; Medium preserves the Stage 39 formula; High adds secondary cross-flow filaments. GLSL and MSL shader changes remain uncompiled and unbenchmarked. See `docs/nereid/stage-40-whitewater-quality.md`.


## Stage 41 — optional confluence flow (ECS only)

`WaterConfluenceComponent` is a distinct persistent junction entity linking 2–4 generation-safe upstream water bodies and one outlet. The source is validated in Fixed and sampled only near its explicitly authored mouth. `applyWaterConfluenceFlow` is a shared post-sampling policy in both linear and indexed paths, BEFORE cascade mouth transfer. `WaterSample.flow` changes locally, never source `WaterBody.flow`; no mass conservation, erosion, physics integration or geometry modification. Register through `NativeWaterPipelineConfig::enableConfluences` (false default) or `registerNereidConfluencePersistence`. Presentation scene parity is deferred to Stage 42. Neither compilation nor test execution has been performed. See `docs/nereid/stage-41-confluence-ecs.md`.


## Stage 42 — immutable scene confluence flow projection

`NereidConfluenceScene` is a read-only value snapshot adapter. `snapshotNereidEcsConfluences(world)` maps generation-safe ECS junction and water-body IDs to scene IDs (`packed()+1`). Publish via `WorldScene::setNereidConfluences(...)` at the same ECS read barrier as `setNereidSurfaces(...)`. Both visual ripple advection and per-tile rapid foam apply `flowForWater` (shared Fixed confluence kernel) followed by cascade flow, mirroring ECS sampling. Missing/invalid input snapshots suppress contribution. Scene has no new fluid solver, GPU buffer, geometry modification or persistence. C++/GPU/tests NOT RUN; see `docs/nereid/stage-42-confluence-scene.md`.


## Stage 43 — GPU mesh residency independent of draw-only style

NEREID source mesh revision depends only on body geometry, LOD and vertex tint fields (deep RGB, shoreline foam RGB/strength). Shader-only changes use `NereidSurfaceResident::setDrawStyle`, avoiding VB/IB uploads while preserving full style equality for external callers. `sameNereidSurfaceUploadKey` controls mesh residency and async staging acceptance; pending workers remain bound to immutable geometry revision. This changes no ECS data or GPU uniform layouts. Source-reviewed and committed, but C++/shader/GPU tests have not run. See `docs/nereid/stage-43-material-residency.md`.


## Stage 44 — explicit static JSON confluence source IDs

`parseNereidConfluenceManifest(root,water.surfaces)` parses `nereidConfluences.sources` from a WorldScene JSON manifest. Each junction has a positive scene ID, 2–4 explicit inlet water source IDs, a distinct outlet water ID, a Fixed mouth position, radius, blend, speedLimit and optional enabled. It requires referenced water surfaces to have explicit IDs, and validates X/Z footprint coverage at the mouth. Scene links are NOT ECS Entity handles; the numeric `validWaterConfluenceFlowPolicy` is used by shared flow kernel and scene, whereas `validWaterConfluence` retains full authoritative ECS link validation. WorldScene construction supplies this manifest to its read-only `NereidConfluenceScene`, and later ECS snapshots may override it explicitly. No ECS schemas, scene GPU resources or hydraulic simulation added. Validation NOT RUN; see `docs/nereid/stage-44-confluence-manifest.md`.


## Stage 45 — confluence and material residency regression coverage (authored)

The new CPU-only `ecs_nereid_confluence_contract_test` covers JSON scene authoring and validation (Stage 44), reference/indexed ECS water confluence parity (Stage 41), ECS-to-scene Fixed current transfer (Stage 42), and draw-only vs vertex-baked material upload keys (Stage 43). It is included in CMake/CTest and experimental ECS shell runner. It does not exercise graphics devices, shaders, full persistence/rollback or unrelated genre integration. No test was actually run in the agent environment. See `docs/nereid/stage-45-confluence-contract-tests.md`.


## Stage 45.1 — CPU presentation manifest link contract

NEREID renderer and confluence contract tests both require `worldInteger` / `worldVector`, defined in `engine/world/WorldAssets.cpp`, not `aiverse_engine`. CMake now links `aiverse_world` for these targets; standalone ECS experiment runner compiles `WorldAssets.cpp` and `Sha256.cpp`. Correct linkage has been source-reviewed but not natively built, due unavailable Git checkout and queued CI. See Stage 45 handoff.


## Stage 46 — native CTest fixture corrections

Far-LOD river surface generation deliberately suppresses shore foam to avoid whitening an all-perimeter mesh; its test now asserts no foam in Far and positive foam in Medium. Four intended-valid JSON water examples in the renderer contract test were missing final root braces and are now corrected. Gameplay/renderer behavior unchanged; no native tests run after change. See `docs/nereid/stage-46-native-test-fixtures.md`.


## Stage 47 — library/skill catalog identity

Each registered NEREID CMake package has a distinct skill ID; core water skill remains the common ECS ownership and architecture contract. The packages for buoyancy, interactions, cascades, currents and vortices have separate brief skills and still declare core `nereid-water` as their dependency. The generated skill catalog contains independent references so automated routing does not collide. No water runtime C++ code changes; Python check/CTest not run. See `docs/nereid/stage-47-skill-catalog.md`.


## Stage 48 — large-world ocean LOD test contract

The precise-world ocean patch API honors `WaterSurfaceMeshOptions.lod`: Far emits one unsubdivided quad (four vertices), while default Medium subdivides. Tests must set Far explicitly before asserting vertex count and must test Medium's local-coordinate preservation separately. No runtime changes; rebuild and run native CTest. See `docs/nereid/stage-48-ocean-lod-fixture.md`.


## Stage 49 — repeatable NEREID regression gates

The official CPU validation entrypoint is `bash tools/nereid/validate.sh --focused` or `--full`. Focused mode checks generated AI skills and runs all NEREID ECS/cascade/geometry/render-source contracts, VFX ECS and native RTS integration after building the relevant executables. Full mode builds all targets and runs full CTest. Linux/macOS CPU CI mirrors focused mode in `.github/workflows/nereid-regression.yml`. Per-stage logs and Git HEAD are preserved. GPU optical quality, driver behavior, performance and platform visuals remain separate acceptance gates. See `docs/nereid/stage-49-validation-gate.md`.


## Stage 50 — evidence and reliable Release tests

Mac Studio at `65346f91`: full Release build and 118/118 CTest PASS, focused NEREID suite 16/16 PASS and skill audit 0 errors/warnings. The build log nevertheless had 18 warnings/3 contextual notes from standard `assert()` disappearing with `NDEBUG` in six ECS test sources. In `8f426376ecb095017cefdd313cca5dd6c2d70186`, converted 44 assertions to always-on `AIVERSE_TEST_CHECK`; no runtime logic changed. The new commit requires a fresh full CPU regression because checks previously compiled out will now execute. GPU/Metal quality and FPS/RTS visuals remain unverified. See `docs/nereid/stage-50-release-assertion-correctness.md`.

## Opt-in river grade and current authoring

`WaterRiverGradeComponent` (`nereid.river_grade` schema1) is an optional integer
plane grade anchored in X/Z. Existing WaterBody and geometry bytes are unchanged.
Call `registerNereidRiverGradePersistence`, or set the appended native pipeline
`enableRiverGrade=true` (default false, preserving old registration/checksums).
Reference and indexed sampling, contact depths and the native presentation snapshot
read the same component. Downhill velocity projects onto the nearest river tangent.
Manual mode retains WaterBody.flow; automatic mode estimates terminal velocity
from gravity × grade × responseSeconds, bounded by maxSpeed. This is a linear
resistance approximation, not a discharge-conserving hydraulic solver. Current
fields/vortices still add their existing local behavior.

Manifest/editor `grade` contains `origin:[x,z]`, `slopeDegrees` (±20),
`downhillHeadingDegrees` (0=+Z), `automaticCurrent`, `responseSeconds` (0..60),
`maxSpeed` (0..100). Advanced `risePerMetre:[dY/dX,dY/dZ]` is also accepted;
explicit degrees win. Missing grade preserves flat legacy geometry and flow.
GPU residency keys include grade; mesh vertices and normals use the inclined
plane, and Terra riverbed carving can consume the exact same grade. Horizontal
planar reflection remains an approximation for tilted water. Confluence/cascade
connectors still use their authored reference levels; complex graded joins need
separate validation and must not be called production parity.

Typed `editNereidFlow` validates a candidate before the editor commits Undo/save.
Legacy editor agent ops: `nereid_water_state`, `nereid_flow_set`. HTML5 Studio
controls are implemented on feature/studio-html5 with native bridge methods
studio.nereidFlow / studio.nereidFlowSet; actual frontend/native QA is recorded in
that branch's stage handoff. Changing water grade does not rebuild an already
exported terrain: rebuild from the Terra recipe using alignRiverGrade=true.
Water animation uses render seconds; its velocity/direction come from NEREID
configuration, independently of clouds/wind and foam intensity.

## Read-only footprint clearance for Terra authoring

`NereidFootprintDistance.h` provides `waterFootprintDistance(body,geometry,point)`
as an optional signed Fixed horizontal margin. Enabled invalid geometry fails
closed; a disabled override uses the original body footprint. Infinite water
uses a bounded negative sentinel. Tapered rivers use projected centreline margin,
not an exact Euclidean distance to their variable-width boundary. This helper
has no state, RNG, persistence/schema or contact-selection changes. Existing
`coversWaterBody`, linear/indexed sampling, priorities and save checksums retain
their original algorithms; do not replace membership tests with rounded distance.
Terra authoring combines clearance with graded height for one sand/wetness/grass
snapshot. It does not install a live hydraulic or erosion solver.

## Camera-independent flow presentation snapshot (CPU stage)

`engine/world/NereidFlowMap.h` builds a bounded data-only world-space grid from
`gradedWaterBody` + `waterCurrentWithVortices`. It samples the local graded water
surface, preserves manual flow and existing current/vortex controls, and never
writes to ECS. The default grid is 2m, at most256 per axis and16384 texels; larger
footprints increase spacing by powers of two. Finite source bounds and sampling
have no camera/mesh-LOD inputs. Infinite water requires an explicit bounded patch;
overlapping same-scale patches retain one world lattice. RG stores X/Z m/s, B
local rapid intensity, A validity. This rectangle is presentation coverage, not
replacement authoritative water membership.

The shared NereidWaterScene prepares immutable linear RGBA16F versions before
drawing. Lit-mesh texture6 and appended U0 waterFlowRect sample velocity at each
fragment world X/Z. Waves use overlapping bounded four-second advection phases;
rapid foam and contact-tail direction consume the same local current. Finite
fields do not rebuild for observer/LOD changes. Infinite patches remain bounded
and world-anchored. Manual flow and automatic grade remain canonical NEREID inputs.

NereidFlowTextureCache bounds active plus pending resources to256 versions/32MiB.
Exact half payload comparison avoids redundant uploads; signed half preserves
negative velocities. CPU captured-frame leases pin replaced versions, then
GpuResourceRetirement waits for GPU completion. Explicit shutdown revokes future
replay before an idle drain, or quarantines handles if idle is unavailable.
NereidFlowBinding is an appended optional renderer argument; existing calls fall
back to per-draw hydraulics, now sampled at a stable source point rather than the
observer. Rebuild native clients for the extended renderer/uniform ABI.

WorldScene supplies its existing confluence/cascade/obstruction resolver when
needed. A trusted nonzero resolver revision permits reuse; zero revalidates a
bounded number of grids each update, exact-deduplicating GPU payloads. Dynamic
many-body refresh latency and CPU cost need host-specific measurement. This is a
presentation snapshot, not a new fluid solver. Metal/desktop GL bend fixtures
verify camera/time-invariant velocity and actual animated shading; editor and
other-platform coverage must be recorded separately in HANDOFF.md. No component,
persistence/checksum, simulation-clock or river-grade save format changes.
