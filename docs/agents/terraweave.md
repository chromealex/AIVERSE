# Agent rules: TerraWeave terrain/foliage/ecology stages 53-65, cosmetic rocks

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## TerraWeave procedural terrain/foliage (2026-11-18)

Universal world generation lives in `engine/world/TerraWeave.h` (namespace
`aiverse::terraweave`), independent of RTS/FPS and usable by native C++
project modules. Read `docs/terrain/TERRAWEAVE.md` and the
`skills/world/aiverse-terraweave/SKILL.md` before implementation.

- Preserve existing RTS terrain rendering, simulation and editor contracts.
  TerraWeave is an optional generic generator, **not** a new parallel terrain
  renderer. Explicitly convert its integer centimetres to old map units.
- Genre-independent components and systems: chunk/vegetation patch entities
  for cosmetic foliage; entities per individual tree only when gameplay needs
  them. Never allocate one ECS entity per grass blade.
- All authoritative generation must be deterministic, independent of chunk
  partition, job timing and input species order. Use stable seeds, integer or
  Fixed math and explicit generator-version migration for saved worlds.
- Generate chunks in pure jobs; ECS changes in deterministic commit order.
  Rendering uploads use the RHI. Shader changes need GLSL+MSL and quality tiers.
- **After every development stage update
  `docs/terrain/AGENT_HANDOFF.md`** with exact commits, API/files, tests
  actually run, limitations and the next step. No tests may be marked passing
  without recorded execution.


### TerraWeave agent ownership for current implementation phases (2026-11-18)

- **Feature/architecture agent** owns TerraWeave runtime functions, native
  APIs, opt-in WorldScene/editor integrations, and a handoff document after
  EVERY implementation stage. Do not make the engine depend on FPS or RTS.
- **Separate validation agent** owns build/CTest, the unified test runner,
  Metal/GL/Vulkan screenshot checks, race/sanitizer checks, FPS/RTS
  regressions and performance benchmarks for this feature. The feature
  agent should NOT duplicate that test work while the user requests
  functions first. This task split never permits claims that unexecuted
  tests passed. Record all tests as NOT RUN until the testing agent
  provides actual build/report evidence.
- New `HorizonSurfaceSources`, bounded memory cache, camera hysteresis,
  Horizon quality presets and `TWH8` prebaked atlas archive interfaces
  are documented in `docs/terrain/TERRAWEAVE.md`. Keep their generator
  versions/fingerprints consistent whenever the bake format changes.
  Don't introduce file-system writes or game-authority mutations
  into rendering/presentation code.

### TerraWeave Horizon terrain borders and offline atlas migration (2026-11-18)

- Horizon far-terrain grid world coordinates **must** use the globally
  aligned signed rational lattice in `engine/world/TerraWeaveHorizon.h`,
  not camera-local interpolation. Border normals use ghost-neighbor
  samples and match on overlapping regions if the height source matches.
- `terrainSkirtDepthCm` only adds outer downward geometry to existing
  terrain primitive/material 0; no additional backend, model or ECS.
- `TerraWeaveHorizonArchive.h` currently uses **TWH3** and invalidates
  TWH1/TWH2 assets. Re-run `aiverse_horizon_bake` for static game packs.
  Preserve current archive version/fingerprint semantics when extending
  Horizon geometry; never silently read old incompatible assets.
- After each functionality stage, append to
  `docs/terrain/AGENT_HANDOFF.md`. Another agent runs ALL
  compiler/CTest/platform-GPU validations; label them NOT RUN here.

### TerraWeave Horizon near/far terrain ownership (2026-11-18)

- The current Horizon archive is **TWH4**, not TWH3.
  It stores an optional authored near-ground ownership map,
  inner seam lip and stable skyline settings; all previous
  TWH1–TWH3 binaries must be regenerated. Maintain archive
  version/fingerprint whenever changing geometry layout.
- Do not remove far terrain merely because it lies
  within a near-camera radius if `WorldScene` exposes
  actual authored terrain coverage. Use immutable
  `HorizonSurfaceSources::ownsNearTerrainRect` and
  conservative `AuthoredWorldHeightSnapshot::coversRect`;
  absent near tiles must keep far ground visible.
- Inner near/far seam lips are strictly bounded to
  512 segments, share the original GPU Model/material 0,
  and do not create gameplay ECS data.
- Horizon outer grid snaps to a **nearest global lattice
  anchor** in coarse steps to stabilize mountain silhouette;
  forest radius must not extend beyond guaranteed far
  terrain coverage. Separate agent owns all build/CTest/
  Metal/GL/Vulkan screenshots and benchmarks.

### TerraWeave exact near-ground clipping / archive TWH5 (Stage 18)

- **Current archive:** `TerraWeaveHorizonArchive.h` is
  **TWH5**. Any preceding TWH1–TWH4 reference is
  historical; old binaries must be regenerated.
- Distant terrain overlapping a partial authored
  near-tile rectangle should be subtracted on the CPU
  using `planHorizonNearGroundClipping`, preserving
  original far-ground triangle plane and winding.
  No new RHI backend, material or draw per near tile.
- The immutable source snapshot supplies both
  `ownsNearTerrainRect` and `collectNearTerrainRects`.
  Input rectangles and output mesh triangles are
  strictly bounded; on cap overflow fall back to
  the conservative full coarse quad cutout rather
  than unbounded memory.
- `maxNearClipTriangles`: Minimal=0, Mobile=512,
  Balanced=2048, High=4096, configurable up to 8192.
  Accurate X/Z clipping is NOT proof of seamless
  vertical near/distant terrain heights.
- Update `docs/terrain/AGENT_HANDOFF.md` on each
  source stage with files/commits/actual tests.
  Testing is owned by a separate agent; do not
  claim compilation, screenshots or passing CTest
  without that agent's logs.

### TerraWeave Horizon TWH6 near-surface seam data (Stage 19)

- Current archive is **TWH6**; old TWH1–TWH5 blobs are historical
  and must be regenerated. The version/fingerprint changes when the
  baked seam sample layout changes.
- Authored-world near/far stitching must capture near-terrain heights
  from the **same immutable snapshot** as resident rectangle coverage.
  Run `populateHorizonNearSeamSamples` after `bakeHorizon`
  in both CPU worker and offline batch, BEFORE archiving/caching.
- Seam sampling uses signed world-centimetre coordinates with
  Q16 precision, no more than 4096 unique points, and an
  all-or-none fallback when required heights are missing.
  The source `nearGroundHeightAtCm` returns nullopt when
  near terrain is not resident. The graphics/RHI layer must
  not query live ECS/collision heights during submission.
- `makeHorizonModel` welds the boundary vertex Y into the
  existing material-0 triangles without adding a new model
  or draw call. Real GPU seam quality is still unverified;
  the separate testing agent owns CMake/CTest/driver captures.

### TerraWeave Horizon slope-safe near seam and TWH7 (Stage 20)

- **Current archive TWH7:** `TerraWeaveHorizonArchive.h`
  includes bounded near-weld slope/normal settings and the
  weld-fallback flag. TWH1–TWH6 are historical and must
  be rebaked with `aiverse_horizon_bake` or regenerated.
- `maxNearWeldSlopePermille=2500` (0=off) preflights
  all new welded clip triangle gradients. An unsafe weld
  is fully disabled for its region; precise X/Z clipping
  remains and `nearWeldSlopeFallback` is explicit.
  Do not misrepresent a missing weld as seam-free geometry.
- `nearSeamNormalBlendPercent=65` repairs shared clipped
  vertex normals with area-weighted geometric face normals
  during existing CPU Model construction. No changes
  to collision, near terrain ownership, GPU topology,
  materials or backend APIs are permitted for this stage.
- `HorizonRuntimeStats` exposes smoothed normal count and
  slope safety fallback through the GPU LRU. User assigns
  compilation, CTest, GPU screenshots and benchmarks
  to another dedicated testing agent; always report
  `NOT RUN` unless actual evidence is provided.

### TerraWeave TWH8 exact fractional near-normal seam contract

- **Current Horizon archive is TWH8**. Historical TWH1–TWH7
  blobs must be regenerated; exact Q16 near seam heights,
  signed near-surface gradient components and normal-valid
  flags are versioned and fingerprinted through the
  Horizon archive.
- `AuthoredWorldHeightSnapshot::sampleQ16` is an optional
  fractional-centimetre source contract used only by the
  CPU seam baker. `WorldScene` supplies
  `nearGroundHeightAtQ16Cm` from the SAME immutable
  near-ground snapshot as near tile ownership.
- On the near/far seam, use authored near-surface normals
  when available, otherwise the existing area-weighted
  far triangle normals. The `maxNearWeldSlopePermille`
  all-or-none seam welding guard remains active.
- `horizonCanReuseModelGeometry` must reject changes
  in Q16 seam positions, near height, slopes, normal
  validity and normal blend settings for **atlas-only**
  GPU patching; never leave stale vertex normals.
- `HorizonRuntimeStats::matchedAuthoredNearNormals`
  measures actual near-surface normal matches, not GPU
  performance. Separate testing agent performs all
  CMake/CTest/Metal/Vulkan/GL/benchmark validation.

### TerraWeave Stage36 host quality policy for Horizon GPU wind

Native projects may use `engine/world/TerraWeaveWindUploadPolicy.h`:
`HorizonWindUploadPolicy{maxBytesPerUpdate=2MiB,maxSparseRanges=8,
fullFallbackPercent=70}`. `WorldScene::setVegetationHorizonWindUploadPolicy`
forwards to Horizon's `setWindUploadPolicy`; validate strictly before
publication. Zero byte budget defers changed colour transfers; a too-low
budget may indefinitely defer dense fallback while the safe macro
shader remains. Combined active+outgoing GPU upload limit is per update,
not per model. The **2 MiB per-model CPU shadow remains fixed**; no
new GPU resources or TWH8 format changes. See
`docs/terrain/TERRAWEAVE.md` Stage36 and
`docs/terrain/AGENT_HANDOFF.md`. User prioritizes features before QA;
do not claim native/Metal/GL/Vulkan verification.

### TerraWeave Stage37 — safe inactive wind CPU shadow budget

Use `engine/world/TerraWeaveWindShadowCachePolicy.h` and public
`WorldScene::setVegetationHorizonWindShadowCachePolicy` for a
0..16MiB budget on **inactive GPU cache wind overlay capacity**.
It is separate from Stage36 `HorizonWindUploadPolicy` and does not
cap active/outgoing wind overlays. When cached shadow capacity
exceeds the policy, `HorizonGpuRegionCache` evicts the entire oldest
shadow-bearing **GPU model plus CPU overlay** via the RHI owning
thread; never detach the CPU shadow from a live GPU model whose
RGB bytes may already hold exact PHYSIS data. Re-entry rebuilds
via the existing Horizon worker/CPU bake/asset archive and uploads
a coherent new model/shadow, not a fabricated GPU readback.
`gpuCachedWindShadowBytes` and `gpuWindShadowBudgetEvictions` in
`HorizonRuntimeStats` expose current dormant capacity and specific
evictions. Update `docs/terrain/AGENT_HANDOFF.md` each stage;
CMake/CTest/Metal/Vulkan/GL **NOT RUN** under feature-first request.

### TerraWeave Stage38 — lossless GPU-cache CPU shadow packing

`HorizonWindShadowCachePolicy::compressCachedShadows` is opt-in and
defaults to false. Inactive Horizon GPU LRU entries may losslessly
compress their full `ModelVertex` wind CPU shadow with
`TerraWeaveHorizonWindCompression.h`. Never detach or compress live
active/outgoing shadows; never infer the GPU's exact PHYSIS RGB from
a procedural generator. On cache `take`, fully unpack and validate
the CPU shadow BEFORE the model becomes active or Stage35 sparse RHI
writes resume. A corrupt compressed stream retires the matching GPU
model and falls back to ordinary re-stream; failed allocation still
propagates. Count both raw and packed storage capacity toward Stage37
inactive CPU budget; do not claim a memory benefit unless measured.
Source/PHYSIS/TWH8 semantics, signed coordinates, shader graphs and
all genre-independent ECS ownership remain unchanged. QA is NOT RUN
under the user's feature-first order; update Stage38 handoff on branch.

### TerraWeave Stage39 — runtime Horizon presentation quality profiles

AIVERSE already has `HorizonSettings` geometry quality
`Minimal|Mobile|Balanced|High`. Stage39 adds an **independent**
`HorizonPresentationPolicy` from
`engine/world/TerraWeaveHorizonPresentationQuality.h` with
`BatterySaver|Mobile|Balanced|Desktop` presets. Compose existing
exact/procedural wind shader controls, Stage36 sparse GPU transfer
budget and Stage37/38 inactive wind CPU shadow/cache compression;
do not create parallel renderers, terrain bakes or PHYSIS solvers.
The *optional* manifest `terraweaveHorizon.presentationQuality`
(`battery-saver|mobile|balanced|desktop`) must not alter the
preexisting geometry `quality` key or TWH8 cache fingerprints.
No new field = Stage38 defaults. Native game code may set the whole
policy on `WorldScene` or `TerraWeaveHorizonScene` and override
individual components later. Validate all fields before mutation
and use scene-owner threading. Report effective settings (not
assumed named presets) and do not claim measured speed/battery
improvement; CMake/CTest/GPU QA remains **NOT RUN** until the
separate testing stage. Record the actual code SHAs and continuation
in the per-stage `docs/terrain/AGENT_HANDOFF.md`.

### TerraWeave Stage40 — host-fed adaptive Horizon runtime quality

`engine/world/TerraWeaveHorizonAdaptiveQuality.h` defines an
opt-in, strictly bounded controller over Stage39
BatterySaver/Mobile/Balanced/Desktop **presentation** policies.
The user requires reusable genre-independent runtime mechanics:
do not modify Horizon terrain/forest geometry, TWH8 archives, GPU
buffer types, PHYSIS source ownership or ECS simulation to adapt.
A native host must feed actual CPU and/or completed GPU frame times
in **microseconds** with monotonically increasing frame IDs via
`WorldScene::submitVegetationHorizonFrameTiming`. Zero denotes
unavailable measurements, not a fabricated 0ms frame.
`gfx::Device::gpuFrameMs()` may be zero/delayed; always respect
the actual backend timing availability and sample ordering.
The backend-free policy requires sustained threshold violations
(30 downgrade frames / 240 upgrade frames by default), 240-frame
cooldown, and one-level bounded transitions; gaps and invalid
samples cannot drive switches. Manual Stage39 policy/preset changes
must disable adaptation, environmental/PHYSIS inputs do not.
`terraweaveHorizon.adaptiveQuality.enabled` explicitly opts in
at startup but does not instrument the host's frame.
Keep source-only unverified stages marked **NOT RUN**, document
exact code commits/risks/next step in
`docs/terrain/AGENT_HANDOFF.md`, keep PR #24 Draft until
a separate tester compiles and profiles real platforms.

### TerraWeave Stage41 — native client frame timing ownership

AIVERSE already exposes asynchronous tagged `gfx::Device::timeGpuFrame`
and `takeGpuTimings`. Stage41's
`engine/world/TerraWeaveHorizonFrameTimingBridge.h` connects real
measured CPU callback durations plus asynchronous tagged GPU samples
to Stage40's optional adaptive `WorldScene` controller. The bridge
is included in both `apps/client/GameClient` (platform begins/ends
the RTS frame) and `apps/fps/client.cpp` (FPS explicitly begins/ends
its device frame). Do not add duplicate `beginFrame/endFrame` calls,
use simulation `dt` as measured CPU/GPU time, or treat
`Device::gpuFrameMs()` from an old completed frame as a sample for
the current one. Tagged GPU completions are matched to original
frame IDs; unsupported/late results use bounded CPU-only fallback.
The `takeGpuTimings` API destructively drains **all** ready GPU tags,
so a future app using multiple timing subsystems needs one shared
device timing collector; do not silently race two consumers.
No frame query when `adaptiveQuality.enabled` is false; no
simulation, PHYSIS, terrain geometry, TWH8 or shader change.
Build/tests/native Metal/GL/Vulkan timings **NOT RUN** until the
separate agent performs QA. Continue updating the Stage41 handoff
in `docs/terrain/AGENT_HANDOFF.md`.

### AIVERSE Stage42 — one GPU timing collector per graphics Device

The shared RHI now owns `gfx::Device::gpuTimingRouter()`
(`engine/gfx/GpuTimingRouter.h`). Every new GPU timing client
(TerraWeave, Prisma, SpectraFX/VFX, editor/tools/benchmarks)
MUST use `subscribe`, `request`, `poll`, `take(subscriber)`,
`cancel` and `unsubscribe`, rather than directly draining
`Device::takeGpuTimings()` in parallel. The backend API still
exists but its completed-result drain is destructive across ALL
tags; the router alone should poll it for shared devices. Router
messages restore the original caller's tag and are isolated in
bounded per-subscriber queues; rejected requests retain each
caller's CPU-only/unsupported fallback. Owner-thread only;
never wait for GPU fences, invent timestamps, or claim separate
per-feature GPU costs from a shared Metal command buffer.
Existing native Stage41 RTS/FPS, editor batching benchmark,
RenderBench, GeometryBench and instancing preview were moved
to the generic router. Full API/limits:
`docs/render/GPU_TIMING_ROUTER.md`. No changes to PHYSIS,
geometry, archive TWH8, gameplay ECS or network authority.
Source committed; native CMake/CTest/Metal/GL/Vulkan validation
**NOT RUN** under feature-first instruction. Append exact
per-stage handoff `docs/terrain/AGENT_HANDOFF.md`.

## TerraWeave + NEREID shared ecology ownership (2026-11-19, Stages 53–56)

- User assigned further water/shoreline erosion development to the **TerraWeave integration agent**. NEREID remains the single owner of ECS water bodies, flow, waves/currents and water contact. TerraWeave owns changed terrain, dig/fill, soil materials, root/plant support/falling and grass aggregate coverage; no independent second water physics.
- Shared deterministic ECS workflow: register `registerNativeWaterPipeline(...enableBankErosion=true)`, `registerTerraWeaveBankVertexPersistence` and `registerTerraWeaveAuthorityResource`, then add `addTerraWeaveAuthoritativeBankSystem` **after** NEREID's water bank system. Never calculate `erodeShore` a second time from ECS results, and never have standalone `NereidEarthworksBridge::step` mutate the same bank vertices. The canonical rollback/save state is `TerraWeaveAuthorityResource` schema v2; graphics/RHI, NavMesh and Box3D are derived and rebuilt at the owner-thread barrier.
- `TerraWeavePersistence.h` is an optional snapshot codec, `TerraWeaveReplication.h` is compact tick/revision-guarded per-change transport-neutral data, and `TerraWeaveEcsAuthority.h` is the actual Simulation World resource writer. Read **all** handoffs `docs/terrain/stage-53-persistence-ecs-handoff.md` through `stage-56-native-example-handoff.md` and `examples/world/terraweave_native/README.md` before touching these APIs.
- After an ECS tick/rollback: use `NativeTerraWeaveWorldBinding` + `publishNativeTerraWeaveFrame` to publish pinned physics terrain, rebuild existing `nav::Mesh`, near/far forest and bounded grass GPU models. Trigger falling tree/capsule Box3D only from committed server events; replicate server poses, never claim cross-platform float Box3D lockstep. Fallen-tree visibility is separate from harvested tree removal; grass does not have one entity per blade.
- **Status:** Source committed but Stage 44+ ecology/erosion/native additions have NOT passed new builds/tests or visual QA. The older 9/9 TerraWeave CPU and 1/1 GPU timing router tests do not prove new features. Draft PR #24 should not be merged to main until separate native QA, collision/NavMesh, NEREID replay, Box3D, GPU/Metal and full regression tests are green. No volumetric caves/overhangs yet; heightfield digging is not voxel excavation. Preserve per-stage handoff documents, tested/not-tested status, exact commits and next actions.


## TerraWeave Stage57–59 shoreline auto-discovery and sediment ownership (2026-11-19)

- `engine/world/TerraWeaveShorelineRaster.h`: discover bounded wet/dry bank and optional interior wet-bed grid cells using REAL `lib::nereid::WaterQueryIndex` over canonical water ECS bodies with polygons/river splines; classify soil via authored callback/optional biome helper, wave splash, slope, root reinforcement. `refreshAutoShorelineBanks` creates/persists auto-tagged ECS bank entities only at a structural barrier. It MUST NOT overwrite manually authored bank ECS entities, must retain suspended sediment when water retreats and must reject stale Earthworks ground heights. Register `terraweave.auto_bank` persistence during setup.
- `engine/world/nereid/NereidSedimentTransport.h` performs **mass-conserving**, bounded, integer-only transport of suspended sediment across neighbouring same-water-body cells, or across an EXPLICIT NEREID-confluence/cascade directed inlet→outlet link. Micro-cm carry persists so low Fixed currents eventually move small amounts. Sediment transport itself never edits terrain height; it is fed into existing NEREID `erodeShore` deposition/erosion on following ticks.
- `engine/world/TerraWeaveSedimentEcs.h`: register `nereid.bank_sediment_transport` ECS persistence once; order native systems: NEREID sample → local bank erosion → sediment transport → TerraWeave canonical ECS Earthworks commit. The ECS transport stage validates confluence mouth geometry and cascade linkage, uses existing `WaterContactComponent.sample.flow`, and does not create another water solver or rerun erosion.
- `examples/world/terraweave_native/{Integration.h,README.md}` demonstrates optional registration, bounded autoregistration, submerged bed carriers and soil material callbacks. **Do not** scan the full coast every graphics frame; rescan at map start or topology edits on an ECS structural barrier. Grass still uses aggregate patches, not per-blade entities.
- **COW rollback correctness:** `World::prepareExistingWrite<T>()` alone does not detach shared pool ownership. Use `World::pool<T>()` before direct writable ECS component access; the pool then detaches individual pages. Fixed related NEREID sampling/erosion and auto-bank/sediment ECS authorship in these stages. Preserve this contract in further code.
- Read `docs/terrain/stage-57-auto-shoreline-handoff.md`, `stage-58-sediment-transport-handoff.md`, `stage-59-riverbed-delta-handoff.md`. New Stage57–59 code is **NOT compiled or tested yet**; old TerraWeave CPU 9/9 and GPU timing router 1/1 successes predate this work. Keep PR #24 Draft. Remaining: native ECS/gameplay/Metal QA, automatic rescan scheduling, downstream sediment mixing at wider mouths, physical river rerouting/landslides, and water-volume feedback.


## TerraWeave Stage60–61 — collapse of undercut soil (2026-11-19)

- `engine/world/TerraWeaveSlopeFailure.h`: `SlopeSoil` carries NEREID/PHYSIS-sampled wetness, soil type and root strength. `planSlopeFailures(earth,triggerVertices,slopeSoil,settings)` is an integer-only deterministic bounded heightfield relaxation: unstable higher neighbours transfer equal-area centimetre mass downhill, rock is immobile, bedrock/buffer limits are enforced, each of at most 4096 disjoint pairs participates once per tick. `applySlopeFailures` commits the plan through one Earthworks sparse transaction for generic native games.
- `engine/world/TerraWeaveEcsBankBridge.h` additionally accepts opt-in slope failure planning after NEREID computes bank erosion. **Merge water erosion AND soil slides into a single final Earthworks revision**, then recompute root support/fall and sparse grass damage ONCE from the final ground. ECS bank soil corrections are recorded for adjusted bank grid vertices, retaining NEREID suspended sediment/carries. Never call `erodeShore` twice.
- `engine/world/TerraWeaveEcsAuthority.h`: opt-in `addTerraWeaveAuthoritativeBankSystem(...,enableSlopeFailures,settings)` writes the canonical ECS resource once and updates only extra bank soil heights after prevalidating/detaching COW pool pages. `excavateAuthoritativeEcs(...,enableSlopeFailures=true)` supports digging-induced collapses and also synchronizes affected NEREID bank soil heights. Both return sparse slope, tree fall and grass damage records usable by native gameplay/Box3D.
- `examples/world/terraweave_native/{Integration.h,README.md}` opts in to bank slope failure; graphics/physics/navmesh publication stays with the existing `TerrainMutationCoordinator` native owner-thread barriers. Read `docs/terrain/stage-60-wet-slope-failure-handoff.md` and `stage-61-nereid-slopes-ecs-handoff.md`.
- **Quality status:** Source committed; NO native compile/CTest, Box3D fall, Metal screenshot, geotechnical rate calibration or ECS replay run for Stage60–61. Earlier green 9/9 CPU and 1/1 GPU router tests predate these features. Keep PR #24 Draft. This is a heightfield slump, NOT a volumetric cave/overhang or 3D fluid simulation.


## TerraWeave Stage62 — physical landslide debris and NEREID current blockage (2026-11-19)

- User delegated continued water/terrain ecology to TerraWeave integration agent. **NEREID alone** owns the ECS `WaterBodyComponent`, water geometry, current/vortex/confluence/cascade resolution, bank erosion and local sediment transport. TerraWeave owns terrain slide events, Box3D debris proxy generation, tree fall mechanics and native ECS/current-obstacle publication. Never implement a second water solver or alter `WaterBodyComponent.flow` in a terrain worker.
- New `engine/world/nereid/NereidFlowObstacles.h`: opt-in persistent `WaterFlowObstructionComponent` and deterministic fixed-point `deflectNereidFlow` kernel. Reference `sampleWater` and `WaterQueryIndex::sample` apply it *after* ordinary NEREID currents and mouth policies. Source key sorting must be consistent in both query paths; cap active overlapping obstacles to 256. Native config `NativeWaterPipelineConfig::enableFlowObstructions` enables schema persistence, off by default in unrelated games.
- `engine/world/TerraWeaveSlopeDebrisPhysics.h`: optional bounded server-owned Box3D dynamic fragment proxies from `SlopeFailurePlan` (max default 64 new/tick, 256 living), with stable event IDs, lifetime pruning, and rendering via borrowed soil prototypes. Earthworks ALREADY transferred mass; debris bodies are visual/collision proxies, NOT a second mass ledger. Keep Box3D behind an opt-in native link and replicate server poses, do not assume float lockstep on clients.
- `engine/world/TerraWeaveFlowObstructionEcs.h` is the **Box3D-free** native ECS writer for generated auto obstacles (distinct from manually authored ones). Tag `AutoFlowObstructionComponent` marks generated records and is separately persisted. Run `publishNereidFlowObstructions` ONLY at the ECS structural-write barrier AFTER the server Box3D step and BEFORE the next NEREID sample. Preserve World COW isolation.
- `engine/world/TerraWeaveNereidObstacleBridge.h` (requires Box3D) reads `FallenPlantPhysics` and `SlopeDebrisPhysics`, quantizes poses to Fixed and resolves the owning NEREID water body with existing `sampleWater`. `synchronizeNereidPhysicalObstacles` writes generated ECS obstacles and maps to the immutable WorldScene flow snapshot. Native game MUST supply ECS body Entity → `NereidSceneSurface.id` mapping, not assume Entity.index equals scene ID.
- `WorldScene::setNereidFlowObstructions` affects optional whitewater shader current and ripple drift via the *same* `deflectNereidFlow` kernel. Visual water does not create or mutate gameplay water bodies, water height/volume, or solve fluid pressure. Existing manually authored scene/NEREID waters remain independent.
- Example: `examples/world/terraweave_native/Integration.h` registers fixed ECS water/soil/obstruction components without Box3D; `PhysicsIntegration.h` is the opt-in owner-thread post-ECS Box3D and after-physics water-feedback adapter; `README.md` describes exact update order.
- Full handoff: `docs/terrain/stage-62-debris-water-feedback-handoff.md`. **QA:** Source committed; no new Clang/CMake/CTest, Box3D scene, ECS replay, Metal screenshots or performance verification yet. PR #24 MUST stay Draft. Deeper hydraulic rerouting/pressure/river volume feedback, precise per-branch collisions, compact water-obstruction network transport and native FPS/RTS opt-in remain future work.


## TerraWeave / NEREID Stage63 — finite-volume flood routing (2026-11-19)

- NEREID is still the ONLY owner of water. NEW `engine/world/nereid/NereidFloodRouting.h` is an **optional NEREID extension** (native `NativeWaterPipelineConfig::enableFloodRouting`, off by default for unrelated projects) for local, bounded equal-area water-column routing. `WaterFloodResource` is named persistent ECS resource `nereid.flood`, storing 16k max sorted signed grid cells, column water depth/volume, originating WaterBody Entity, TerraWeave ground height, flow and soil/wetness/tick revisions. `stepNereidFlood` transfers ONLY stored finite reservoir volume along hydraulic-head gradients between neighbouring cells, with conservative before/after mass equality and NEREID `WaterFlowObstructionComponent` permeability. Water can pile up behind a fallen tree, move sideways, flood a formerly dry cell and recede on later ticks. Do NOT invent water from rain/flow outside the finite reservoir.
- `NereidWaterSystems.h` and `NereidQueryIndex.h` apply `applyNereidFloodSample` after existing water-body/flow/obstacle resolution; a flooded cell outside the original polygon/spline footprint becomes a real WaterSample for downstream NEREID interaction/erosion. If the authored water surface is already higher, that existing surface wins.
- `engine/world/TerraWeaveNereidFloodBridge.h` seeds an explicit finite grid ONCE from existing WaterBody + canonical Earthworks; calling `initializeNereidFloodGrid` on an already active reservoir is prohibited (would create/remove volume). Ground synchronization updates grid-bottom elevations after TerraWeave digging/bank erosion/slump, without changing any water-volume column. `addTerraWeaveFloodGroundSyncSystem` must be registered after the TerraWeave soil ECS commit for the NEXT water tick.
- `WaterFloodResource::wetnessRevision` tracks new wet/dry cells separately from sub-cell volume changes. `engine/world/TerraWeaveFloodShorelineSystem.h` provides persistent `AutomaticShorelineRevision` and opt-in last-stage ECS auto-bank refresh when groundwater/flood topology or soil revision changes; newly generated bank cells participate in NEREID sampling NEXT tick. Explicit `requestAutomaticShorelineRefresh` is required for water-body geometry edits not reflected by soil/wetness revision.
- `engine/world/NereidFloodVisualScene.h` and `WorldScene::setNereidFloodCells` upload/draw a maximum 4096 flooded surface quads via ONE existing RHI model and transparent/unlit fallback material (native driver publishes up to 2048 active cells). Only extra water beyond/above base NEREID surface renders an overlay. It does NOT yet share the full NEREID refraction/foam shaders or exactly blend with authored curved coastlines.
- Native example: `examples/world/terraweave_native/{Integration.h,README.md}`. Registration order: NEREID flood routing BEFORE NEREID water sampling/erosion; TerraWeave bank soil/roots/grass commit; flood ground sync; OPTIONAL auto-rasterized shoreline LAST. All owner/thread and COW barriers preserved. Read `docs/terrain/stage-63a-nereid-flood-volume-handoff.md` and `stage-63b-flood-visual-native-handoff.md`.
- **Crucial limit:** volume conservation is within the finite, explicitly seeded equal-area flood window only; legacy static WaterBodyComponent levels and infinite ocean/river reservoir amounts are not hydraulically coupled. No rainfall/pumps/open-boundary inflow, sediment-dependent pressure solve, full shallow-water momentum or actual Navier–Stokes. Do not claim such features, global mass conservation including authored base water, or working Metal without tests.
- **QA status:** Stage63 source committed to `feature/terraweave` but not CMake/Clang compiled, CTest run, ECS replay verified, Box3D flood-simulated or Metal screenshot validated. Keep PR #24 Draft; previous TerraWeave 9/9 and GPU router 1/1 are older revision evidence only. Next: native smoke compilation, tests for 1000-tick volume conservation/overtopping/reference-indexed sample parity and GPU scene validation, followed by perf.


### TerraWeave/NEREID Stage63c finite river boundary flow (2026-11-19)

- Closed `NereidFloodRouting.h` grid alone has no infinite inflow: a flat initial river surface cannot automatically build upstream head. NEW `WaterFloodPort` (Inflow/Outflow, at most 64) and `WaterFloodResource::externalVolumeCm` explicitly model a finite, already-counted outside reservoir. Port water transfers are bounded by user-defined cm-per-tick rates and local cell capacity. Water conservation is checked over **on-grid volume PLUS externalVolumeCm**, preventing fake water creation when a blocked channel backs up or a low bank overtops.
- Configure only before the first flood tick, after seeding the grid and WaterBody entities, via `terraweave::configureNereidFloodBoundaryPorts` or native example `configureFloodRiverPorts`. `NereidFloodRouting.h` persists these ports/reserve in schema `nereid.flood`, validates inlet generation-safe WaterBody ID and port locations. A finite reserve can run dry; outlets return water to the SAME counted reserve. Do NOT call configuration again after the ECS flood tick or silently reseed/migrate the grid.
- Exact result metrics `WaterFloodTick::suppliedCm/drainedCm` and old/new water column totals, `wetnessRevision` from FINAL wet/dry changes, and `WaterFloodResource::groundRevision` for TerraWeave soil sync. Scene overlay only represents water beyond/above existing static water surfaces; full NEREID refraction/foam not yet integrated.
- See `docs/terrain/stage-63c-finite-river-ports-handoff.md`, `stage-63a-nereid-flood-volume-handoff.md`, `stage-63b-flood-visual-native-handoff.md`, and `examples/world/terraweave_native/README.md`.
- **Feature-only commits, NOT compiled/tested.** Do not call this globally volume-conserving (static ocean/river bodies are outside the ledger), a pressure/momentum solver, or production-ready flood rendering. PR #24 stays Draft.


## TerraWeave Stage64 — grass ecology and recovering riverbank surface protection (2026-11-19)

- Native ecosystem code: `engine/world/TerraWeaveShoreRecovery.h` (pure integer recovery policy, versioned `ShoreRecoveryResource`, one small persistent `ShoreRecoveryPatchState` per aggregated `GrassPatch`, seeded regrowth after drying, prolonged flooding damage, nutrient/moisture/light/age response and spatial `ShoreRecoveryCoverIndex`). NO ECS entity per blade and NO separate vegetation renderer, water or soil mass solver.
- `engine/world/TerraWeaveShoreRecoveryEcs.h`: `addTerraWeaveShoreEcologySystem` runs after NEREID flooding/shore erosion/sediment and after TerraWeave bank/slump and auto-shoreline refresh. Builds ONE NEREID WaterQueryIndex at ecology cadence (default 600 ECS ticks) and takes 5 terrain/water samples per grass patch, publishing canonical `TerraWeaveAuthorityResource.grass` changes and separately persistent `terraweave.shore_recovery` with its seed, wet/dry and soil-cover states. Owner-thread staging World copies keep rollback state coherent; beware non-COW deep-copy cost on huge Worlds. Tree/shrub fall state is untouched; never automatically revive harvested/fallen trees.
- NEREID now accepts OPTIONAL `ShoreErosionForcing::surfaceShieldPermille` and ECS persisted `WaterBankSurfaceCoverComponent` (`nereid.bank_surface_cover`) to reduce exposed soil shearing independently of true tree/shrub root binding. Grass has NO physical tree roots; ecology's aggregated soil/litter protection is a separate factor. Generated `AutoShorelineBankComponent` cells receive updated shield from the 64m bucket `ShoreRecoveryCoverIndex`; manually authored bank cover is game-owned.
- Native example `examples/world/terraweave_native/Integration.h` enables the fixed-period ecology resource/systems by default for this example only and allows disabling/configuring `ShoreRecoverySettings`; existing FPS/RTS/other genre clients remain opt-in. Read `docs/terrain/stage-64-shore-ecology-recovery-handoff.md` for exact committed source steps, API contract, remaining functions and no-test disclosure.
- Stage64 covers grass recovery/surface crust only; detailed multi-species forest competition, adaptive seed distribution, persistent rooted-plant active aging/lighting and full ecological succession remain Stage65+. NO new native compiler/CTest/NEREID simulation/GPU/Box3D/Metal QA yet; prior TerraWeave 9/9 and GPU router 1/1 are older branch. Keep PR #24 Draft.


## TerraWeave Stage65 — rooted forest ecology, canopy light and native saplings (2026-11-19)

- `engine/world/TerraWeaveForestEcology.h`: per-species **opt-in** budgeted deterministic rooted tree/shrub life cycle over existing `RootAnchor`+`PlantEnvironment` records. `ForestEcologySpecies` configures maturity, age-based death, shade/drought/flood sensitivity, crown spread and seed interval/dispersal; `ForestEcologySettings` caps at default 256 expensive plant decisions every 32 simulation ticks, 65,536 interactable rooted plants total and 8 recruits per step. A persisted cursor and `lastEvaluatedTick` prevent processing every tree each frame. `ForestEcologyCanopyIndex` indexes crowns in 32m buckets and approximates relative-height shade, not photometric GI. Seed positions/IDs use stable integer hashes, exact canonical Earthworks ground and bounded free-space tests; mortality and recruitment never depend on camera.
- `engine/world/TerraWeaveForestEcologyEcs.h` registers persistent `terraweave.forest_ecology` schema v1 (shade/flood/drought stress, mortality, recruitment, processing cursor). An opt-in ECS system reads **existing NEREID WaterQueryIndex** root-zone water/flood samples, optionally PHYSIS sunlight/soil policy, and stages ecology history plus canonical `TerraWeaveAuthorityResource.rooted` mutations together for rollback safety. Post-tick event callback is enqueuing only: never run Box3D or Renderer3D inside ECS.
- `TerraWeaveForestScene::setDeadTrees` is an independent death visibility channel, separate from `setFallenTrees` (water uprooting/Box3D) and `setRemovedTrees` (harvesting). `setRecruitedPlants` uses existing Arbor shared prototypes and Flora near/LOD batching for sparse new saplings, no per-seedling GPU prototype. `TerraWeaveNativeWorldBinding::reconcile` reconstructs their presentation from the new persistent ecology resource after save/rollback. Tree-only changes DO NOT invalidate terrain/collision/NavMesh or unrelated grass GPU batches.
- Native usage: `examples/world/terraweave_native/{Integration.h,README.md}` accepts trailing `rootedForestSpecies`, `rootedForestSettings` and `onForestEcology`. Empty policies leave old games unchanged. For harvested trees, gameplay MUST remove or suppress the canonical rooted entry too: presentation-only `removed_` does not stop a plant from reproducing. Natural death currently hides the Arbor instance but does NOT perform Box3D deadwood fall or game-resource/collider cleanup. Background decorative millions remain procedural, not individually simulated.
- Read `docs/terrain/stage-65-rooted-forest-ecology-handoff.md` for exact files, commits, API, ownership/perf limits and next steps. **No Stage65 native compiler/CTest/ECS/Metal/Box3D/perf tests have been run**; earlier green tests apply to older commits only. PR #24 remains Draft; prioritize a compile and full native QA after feature implementation.

## TerraWeave cosmetic rocks (2026-11-20)
Use `TerraWeaveRocks.h` integer scatter and `TerraWeaveRockModel.h` seeded native
model adapter for stones/boulders/slabs/shards; documentation `docs/terrain/rocks.md`.
Reuse bounded shared prototypes and existing Renderer3D LOD; no plant-age ECS state,
per-rock mesh generation in the render loop, or implied collision authority.
Offline tool `aiverse_terraweave_rocks` exports the same model with shared PBR maps.
The current API is opt-in, not automatically installed in WorldScene/Studio.
Keep separate per-stage handoffs and distinguish visual geometry from physics.


