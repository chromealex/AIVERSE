# NEREID ↔ TerraWeave — shore erosion, earthworks, roots and plant lifecycle

Status: **source implemented on \`feature/terraweave\`, native compilation/runtime QA pending**.
NEREID is the **only** AIVERSE water solver. TerraWeave does not duplicate
WaterBody, currents, confluence, cascade, underwater, rendering or PHYSIS wind.

## Ownership

- NEREID ECS: fixed-point water bodies/current queries and authoritative
  \`WaterBankSoilComponent\` / \`WaterBankErosionOutputComponent\`.
- TerraWeave Earthworks: actual sparse heightfield delta owner and dirty region.
- TerraWeave forest: root anchorage, conditional visual root meshes, plant
  age/light/moisture/nutrients, grass coverage patches.
- Native physics/Box3D/game authority: fall events, rigid-body dynamics, tree
  collision, resource harvesting, saving/replay, navmesh and soil compaction.
- WorldScene/render: forest and Horizon mesh invalidation/GPU uploads on the
  owning render thread only. No ECS mutations in renderer/worker.

## NEREID shore erosion mechanics

\`libraries/include/aiverselib/water/NereidErosion.h\` exposes:

- \`ShoreSoil\`: sand, silt, loam, clay, gravel, non-erodible rock.
- \`ShoreErosionCell\`: ground and bedrock (cm), carried sediment (equal-area
  cm equivalent), fractional erosion/deposition carry, root-binding strength.
- \`ShoreErosionForcing\`: WaterSample flow (Fixed m/s), PHYSIS wave energy,
  wet-bank contact and terrain slope (all validated permille).
- \`erodeShore(..., dtMs, settings)\`: integer deterministic, bounded per-tick
  erosion and deposition; moved soil becomes suspended sediment and vice versa.
  Roots reduce cut rate but the same roots can be exposed and fail once the
  surrounding soil disappears. Dry cells do nothing.

A standalone NEREID soil calculation does not automatically modify a
live WorldScene, collide with water or claim physically complete CFD/sediment
transport. The host must compute wet-bank coverage from real shoreline
geometry (water splines/shore raster/PHYSIS waves), supply it per cell, then
publish changed cells at a well-defined ECS read barrier.

### Optional ECS stage

\`libraries/include/aiverselib/water/NereidShorelineSystems.h\` adds
\`WaterBankSoilComponent\`, \`WaterBankErosionOutputComponent\`,
\`registerNereidBankErosionPersistence(sim)\` and
\`addNereidBankErosionSystem(sim,name,fixedDeltaMs)\`.

\`NereidNativeWater.h\` now accepts
\`NativeWaterPipelineConfig::enableBankErosion\` (default OFF) and
\`bankErosionTickMs\` (1..1000; default 16). The erosion system runs AFTER
water contact sampling. Bank soil and output live in versioned ECS schemas.
Only stable bank-cell entities require these components; never create one
per grass blade.

At a post-tick read barrier, for a changed output and its bank-cell grid
coordinate call \`terraweave::applyEcsBankErosion(EarthworksLayer&,EarthVertex,
WaterBankErosionOutputComponent)\` from
\`engine/world/TerraWeaveNereidSoilBridge.h\`. It validates the former
height and applies the already-calculated output once. **Do not call
\`erodeShore\` a second time** or double-remove sediment.

## Excavation and terrain revision

\`engine/world/TerraWeaveEarthworks.h\` provides \`EarthworksLayer\`
with deterministic sparse grid edits, \`sculpt({centerXcm,centerZcm,
radiusCm,depthCm,mode})\`, single-vertex \`setHeight\`, replay
\`applyEdits\`, \`sampleHeightCm\`, \`revision\` and \`overrides()\`.
Outputs include a touched half-open world-centimetre rectangle, R16-safe
height updates, moved volume and a replay/undo list. A worker may calculate
pure data, but the host commits mesh/collider/navmesh/forest revisions at a
synchronized owner-thread phase.

Flat RTS editor: \`terraweave_excavate\` (MCP + Workspace) changes the
existing corner heightfield via one Undo record, preserves the detail
heightmap's authored residuals and its splat material. This is heightfield
dig/fill: holes, ditches and shore cuts, **NOT caves, overhangs or tunnels**.
FPS, planets and voxel worlds require their own adapters; no genre-specific
assumption belongs in the Earthworks kernel.

## Root exposure and falling trees/bushes

\`engine/world/TerraWeaveVegetationSoil.h\`:

- \`PlantHabit::Tree\`: default root radius 180cm / depth 120cm / 6
  segments; large support footprint
- \`PlantHabit::Shrub\`: 45cm / 25cm / 3 segments; weaker/smaller roots
- \`PlantHabit::Grass\`: **no visible roots**; shallow cover is represented
  only by a GrassPatch coverage fraction

\`evaluateRootSupport(RootAnchor,SoilHeightSampler)\` examines 8 ring samples
against the saved original planting height. Even a natural cliff can expose
roots immediately. Missing samples cannot make a tree fall. Root exposure
beyond the threshold enables \`generateExposedRoots\`; severe loss of
ground support marks \`RootState::uprooted\`. The optional bridge
\`evaluateBankPlant(anchor,snapshot,previous)\` emits one stable fall-event ID
on a standing→uprooted transition. **The native physics/authority host**
must spawn/tip a fallen rigid body and save/replicate the event. It is NOT
correct to kill/remove a healthy tree directly from a render thread.

\`ArborForestAdapter\` lazily builds at most 3 shared root bark models per
exposed species and submits them via existing Renderer3D for near/medium/low
trees. Flat ground causes **no root geometry generation/upload/draw**. Roots
do not attach to grass. The camera impostor/Horizon has no per-root draw.

\`TerraWeaveForestScene::setRootStates\` and
\`world::WorldScene::setVegetationRootStates\` receive saved, stable root
snapshots. An exposed plant remains anchored at its planted height instead
of sinking into the washed-out bank. New root prototype GPU uploads occur
on the next WorldScene owner-device \`update\`. Host is responsible for
re-evaluating soil on a changed dirty region, and disabling/removing
standing geometry after the authoritative falling rigid body takes over.

## Age, light and high-density grass

\`FoliageSpecies\` allows seeded min/max age, maturation/lifespan ticks,
light, moisture, nutrient factors and Tree/Shrub habit.
\`scatter\` deterministically writes those factors into \`FoliageInstance\`
regardless of chunk order. \`TerraWeaveForestScene\` has sparse per-tree
environment overrides \`setPlantEnvironment/setPlantConditions\` and
\`plantConditions()\` for save/replication. \`visibleBatches\` uses
\`plantAppearance\` to scale and adjust foliage health without spawning
more ECS entities or rebuilding every species prototype. Existing worlds
with no ecology inputs maintain their original 100% grown appearance.

For grass, \`GrassPatch\` holds deterministic seed, area (up to 64m),
coverage, environment and a bounded maximum blade count. One
\`GrassPatchDraw\` describes **all** blades; \`sampleGrassBlade\` derives
stable individual age/light/position only as needed by an RHI instance
generator or CPU quality fallback. \`erodeGrassCoverage\` reduces
coverage once per patch, not by iterating millions of blades. This is
CPU draw planning only: an end-to-end GPU instanced/indirect grass
render path and shadow/Lod/mobile fallback still need integration.

## Bounds and validation

Soil/plant properties are integer/fixed and limit-check before commit.
Earthworks stroke at most 65,536 sampled grid points; sparse overlay at
most 1,000,000 overridden vertices. Each grass patch holds up to 262,144
logical blades but never allocates a vector of that length. Exposed root
geometry: max 16 segments, only when flagged. Bank erosion is bounded by
ground bedrock and cut/deposit budgets; no GPU readbacks or wall-clock times.
CPU tests, save/replay checks, integration with Box3D collision, editor,
Metal/GL/Vulkan screenshots and benchmark runs are **NOT YET RUN** for
this stage. The Draft TerraWeave PR must not be merged until reviewed.

## Next implementation scope

1. Wire Earthworks dirty regions into live streamed collision/terrain tiles
   and Horizon/Forest snapshot invalidation, including NEREID waterline
   intersection and safe shore materials
2. Implement Box3D vegetation-fall/rolling rigid-body host adapter and
   saved authoritative events
3. Complete shared grass GPU patch renderer, density/LOD tiers and distant
   HLOD canopy parity, test draw calls and GPU memory on Mac/mobile
4. Follow up with native tests and imagery after functional work
