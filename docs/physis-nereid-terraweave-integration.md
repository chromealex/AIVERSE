# PHYSIS / NEREID / TerraWeave — shared ownership and integration contract

Status: **shared ownership contract + opt-in merged-source checkpoint** (2026-11-19). Main NEREID water code at `d71c6d47e532b25fb816a8bb1f96c41e5275a165` is now included in PHYSIS through merge commit `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b`. PHYSIS's earlier headless tests (24/24), separate editor authoring tests (2/2) and native GUI build were reported PASS before this merge; the **combined PHYSIS+NEREID source has NOT yet been compiled or tested**. An optional read-only NEREID/PHYSIS visual wind bridge exists, but no automatic dual-module authoritative game setup or precipitation-to-water volume physics is claimed.

## Parallel branches and owners

| Capability | Code owner / working branch | Authoritative state and public entry | Must not implement |
|---|---|---|---|
| PHYSIS (nature/climate) | **PHYSIS agent**, `feature/physis-nature-simulation`, draft PR #25 | `engine/physis/`; `aiverse::physis::install`, `environmentAt`, `forEachPresentationCell`; ECS atmosphere, precipitation, surface snow/wetness/ice, fire/quake events | Water-body/query/buoyancy/river/ocean geometry, shoreline geometry, terrain mesh, VFX/render backend |
| NEREID (water) | **Water agent**, merged in `main` at `d71c6d47` and synced into PHYSIS at `7c617d7f`; historical work branch `feature/nereid-water-core` / PR #23 | `libraries/include/aiverselib/water/Nereid*.h`; `aiverse::lib::nereid::WaterBodyComponent`, `WaterContactComponent`, `WaterSample`, `sampleWater`, `registerNativeWaterPipeline` | Reimplement climate/PHYSIS, terrain/foliage generation, genre-specific water forks |
| TerraWeave (land/biome/foliage) | **Terra agent**, `feature/terraweave` | `engine/world/TerraWeave*.h`; `aiverse::terraweave::generateChunk`, `heightAtCm`, `scatter`, flora/arbor adapters | Implement a new climate simulation or water solver; maintain a duplicate mutable water/atmosphere world state |
| Composition / host adapter | **Integration owner (assigned when merging)**, separate integration branch or PR | Optional adapters consuming PHYSIS and NEREID and presenting to Terra; host `Simulation` system ordering, persistence, replay/rollback, cross-module tests | Add bidirectional includes to foundational PHYSIS/NEREID/Terra core or silently migrate legacy RTS |
| VFX/render | **Render/FX agent** (separate) | FX/render presentation consuming immutable snapshots | Advance climate/water/terrain authority or persist GPU handles in ECS |

These are **subsystem roles, not claims about which human is assigned**. When agents create new PRs, record exact owner, branch, head SHA and API compatibility in the handoff. No agent should commit into another active agent's working branch without coordination.

## One source of truth for each property

- **TerraWeave:** static/generator terrain height, biome baseline, soil/exclusions, vegetation descriptors. PHYSIS may read terrain height/biome via an explicit adapter; does not write generated terrain.
- **PHYSIS:** dynamic atmosphere and weather (rain/snow normalized intensities, wind, humidity, temperature); surface wetness/snow/ice and fire/quake components. Generic `EnvironmentalResponse` is the read-only contract.
- **NEREID:** water body identity/shape, surface/bottom elevation, water-contact sampling, current/flow and buoyancy outputs. NEREID is the future canonical water gameplay authority when enabled by the host.
- **Important current overlap:** PHYSIS `HazardCell::waterDepth` / `advanceDisasters` include an **experimental shallow-flood cellular toy model**, not a substitute for NEREID bodies. An opt-in `FloodMode::ExternalAuthority` switch is **implemented and covered by the previously green PHYSIS headless suite, but not yet tested alongside freshly merged NEREID**: it skips local water redistribution/inflow, clears the local shallow-water depth and keeps fire/quake active. When NEREID is canonical water authority, set `InstallOptions::floodMode = FloodMode::ExternalAuthority`. This mode is chosen by the deterministic host config; changing modes mid-save is not a supported migration. An optional read-only presentation bridge (`engine/world/NereidPhysisWindBridge.h`) can forward host-sampled PHYSIS wind into NEREID water visuals; it does not install either subsystem or couple authoritative ECS water to climate. Physical precipitation-to-water-volume conversion is still not implemented.
- Legacy `modules/rts/view/RtsWater.*`, `waterNetwork`, `WaterOps` and baked map cells remain untouched until migration is proven with old tests; NEREID's existing raster import is the opt-in path, not a forced replacement.

## Geometry, units and coordinates (mandatory at boundaries)

| System | Horizontal | Vertical | Values |
|---|---|---|---|
| TerraWeave | signed `int64_t xCm, zCm`, world centimetres | `heightAtCm` / `heightsCm`, signed cm | integer authoritative generation |
| PHYSIS | X/Z grid `Cell{x,z}` with `Settings::cellSizeMeters` + `originX/originZ` | `Cell::elevation`, `Fixed` metres | Q47.16 `Fixed`, signed metre coordinates for `cellAtPosition` |
| NEREID | `Vec3.x/z`, `Fixed` metres | `Vec3.y`, `surfaceY`, `bottomY`, `Fixed` metres | World X/Z horizontal; Y up; `WaterSample` |

- Convert `TerraWeave cm -> Fixed metres` **explicitly**, using checked integer arithmetic; do not cast signed centimetres to metres or assume both generators share the same seed, origin or cell scale.
- Use floor division (including negative coordinates) to select PHYSIS cells; use exact NEREID point/footprint semantics. A PHYSIS cell is not a NEREID water body; many PHYSIS cells may overlap one water body and vice versa.
- Choose a documented terrain sampling convention (cell centre, corners, and NEREID waterline) to avoid shoreline gaps and intermittent flooded/grass terrain.
- For very large worlds, avoid precision loss/overflow in world-to-Fixed conversion; absolute extent and origin-rebasing policies require explicit validation.

## Data flow to Terra (no core dependency inversion)

1. TerraWeave generates/loads terrain chunks and immutable baseline biome/height. Its existing `scatter` remains pure and deterministic.
2. **Host-owned adapter**, not TerraWeave core, samples Terra height/biome and optionally configures PHYSIS `Cell::elevation`, `ClimateZone` and `SurfaceMaterial` before/at deterministic tick barriers.
3. PHYSIS advances exactly **once** per authoritative tick and publishes weather, wetness, snow, ice and hazard read-only snapshots (`environmentAt` / `forEachPresentationCell`).
4. NEREID maintains/query samples of its own ECS water bodies via `aiverse::lib::nereid::sampleWater` or its registered pipeline. **Water agent owns any precipitation-to-water-volume conversion** and water dynamics. Current PHYSIS `rain` is normalized intensity, NOT water depth in metres or flow in cubic metres; do not directly add it to `surfaceY`. Conversion needs calibrated units/timestep and a versioned deterministic policy.
5. Terra's **optional** render/scene adapter uses both: water body/shore masks, shoreline/waterline from NEREID; wetness/snow/ice and wind from PHYSIS; terrain collision/slope/mesh and foliage from TerraWeave. Composition must be read-only to authoritative water/weather; dirty-chunk invalidation occurs only on meaningful data/version changes.
6. Physics, AI/nav, audio and VFX consume the same canonical snapshots; they may return explicit gameplay events at next tick boundaries, not mutate unrelated authoritative systems during their internal update.
7. Legacy RTS views remain opt-in and visually unchanged until dedicated conversion and regression tests pass.

### Expected optional adapter interface (PROPOSAL, NOT IMPLEMENTED)

```cpp
// e.g. engine/world/adapters/TerraEnvironmentAdapter.h (future integration PR)
struct TerraEnvironmentCellView {
    // Converted Terra height/biome + read-only PHYSIS EnvironmentalResponse
    // + optional NEREID WaterSample, with explicit valid/absent flags.
};

// Host owns composition: sample PHYSIS and NEREID independently at a
// documented world-space X/Z position, then produce view/terrain masks.
// Never use a single mutable shared `waterDepth` field for both solvers.
```

A useful first consumer is a CPU-only terrain debug overlay: snow/wetness colouring and NEREID waterline, without modifying authoritative terrain or water. Its contract test should exercise two unrelated native hosts, negative coordinates, multiple PHYSIS cells per NEREID body, shoreline masks, save/load, rollback and module ordering.

## Scheduling and networking

- Initial setup: spawn/load Terra static input; install/register PHYSIS and NEREID persistence in `GameModule::setup` before the registry is sealed; inject baseline material/elevation before the first PHYSIS advance.
- Tick: canonical game input/physics update as appropriate; update PHYSIS once; optional calibrated rain/snowmelt input to NEREID; NEREID water pipeline once; sample contacts and compute optional buoyancy; host physics integrates exactly once. Exact system registration order is project-owned and must be documented/tested.
- Presentation: read only after committed simulation tick; no GPU/OS time/randomness in authoritative calculations.
- Keep rollback/serialization and network hashes on ECS authority only. Do not replicate a separate Terra/PHYSIS copy of NEREID water bodies, or recompute important gameplay water state from GPU waves.

## Acceptance before merging the parallel branches

- Rebase/merge with explicit conflict review of `AGENTS.md`, CMake, test runners, per-stage handoffs; preserve other agents' changes.
- Build and run `./run-tests.sh --filter '^(physis_|ecs_nereid_|terraweave)'`; run the relevant legacy `water_*`, terrain, and native ECS regression tests. **As of this contract: no cross-module build/CTest result has been produced.**
- Verify independent vanilla PHYSIS, NEREID, TerraWeave builds still work. No forced dependency from PHYSIS core to NEREID/Terra, or vice versa; opt-in adapter is its own target.
- Test deterministic output across snapshot restore, save/load and replay; negative coords and centimetres/metres conversion; elevation/waterline coherence; no double flooding.
- Update **root `AGENTS.md`**, each subsystem handoff, this contract and any new public API docs after every stage. New implementation stages require separate code and handoff commits with exact SHA and real test status.

## Working links

- PHYSIS: PR #25 (`feature/physis-nature-simulation`), `docs/physis.md`, `engine/physis/AGENTS.md`
- NEREID: PR #23 (`feature/nereid-water-core`), `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md`
- TerraWeave: `feature/terraweave`, `docs/terrain/TERRAWEAVE.md`, `docs/terrain/AGENT_HANDOFF.md`

## 2026-11-19 — combined native regression checkpoint (Stage 73)

After merging the NEREID water implementation from `main` into
`feature/physis-nature-simulation`, the user ran the focused native
macOS CTest filter `^(physis_|ecs_nereid_|vfx_)` and provided
**40/40 PASS, 0 failures, 10.25s**. Among the overlapping labels
PHYSIS had 26 and SpectraFX had 5 tests. This confirms test-suite
compatibility for the merged sources, NOT automatic gameplay
composition: standalone PHYSIS/NEREID tests may initialize
different World/Simulation instances. A joint host must explicitly
install both systems, choose `FloodMode::ExternalAuthority` for
PHYSIS, define deterministic tick ownership, and validate
save/rollback state and weather/water read-only presentation.
This is still a required integration acceptance step, alongside
full repo CTest, real editor GUI, GPU/Metal water and CI tests.
See `docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

## Stage 78 — per-entity composable contacts (source implemented; not native-tested)

PHYSIS now has an optional `NatureProbeComponent` /
`NatureContactComponent` ECS sampling path in
`engine/physis/PhysisNatureSampling.h`, enabled via
`InstallOptions::enableNatureContacts`. It publishes
immutable-by-convention derived weather, wind, gravity and
hazard values per interested entity after the authoritative
climate step; these contacts are written ECS outputs and may
be persisted only when opted in.

Host-side `engine/world/PhysisNereidNatureBridge.h` reads a
PHYSIS contact and a NEREID `WaterContactComponent` on the
same entity, requiring equal sample ticks and probe positions.
No water-body authority, rainfall-to-volume coupling, solver
or TerraWeave vegetation/terrain mutation is introduced.
Its same-World native test `physis_nereid_contacts` was
registered in CMake, but **native CTest has not run** after
Stage78. Do not claim this as a verified runtime integration
until the new test passes; previous separate 40/40 results
pre-date the new code.
Handoff:
`docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.

## Stage 79 — natural loading and fracture handoff (written, not native-tested)

The PHYSIS project added optional ECS
`NatureStressReceiverComponent` / `NatureStressOutputComponent`
under `engine/physis/PhysisNatureStress.h`. This is a
genre-independent **read-and-publish** layer over existing
PHYSIS `ForceOutput` and `NatureContactComponent`. It records
aerodynamic force, bending moment, load ratio, fire/quake wear,
fatigue and fracture state, but does NOT itself deform terrain,
remove trees or apply Box3D impulses. No duplicate wind solver
or water flow authority was introduced.

- **PHYSIS owner:** stress/condition outputs and deterministic
  environmental causes. Separate opt-in persistence and scheduling.
- **TerraWeave owner:** maps an authored flora/species/root
  resistance and age to stress receiver data; decides when the
  trunk/root breaks, generates roots, removes foliage/grass
  proxies and spawns a fallen tree representation.
- **Box3D/physics owner:** rigid-body/constraint integration
  after a fracture, never by PHYSIS itself.
- **NEREID owner:** bank erosion and undermining effects;
  must report terrain/anchor changes through an authorized
  host event rather than writing PHYSIS damage directly.
- **Large-scale grass:** aggregate sampling over clusters or
  cells; do not instantiate tens of thousands of per-blade
  authoritative stress outputs.
- **VFX owner:** presentation of wind, falling foliage, fire
  and dust; does not own deterministic damage.
- **Test scope:** Stages 78/79 author native CTests
  `physis_nereid_contacts` and `physis_nature_stress`,
  including one-world save/rollback and condition transitions.
  **Neither is natively verified yet**. A real host integration
  acceptance still must validate actual tree fall/Box3D transitions
  plus gameplay state after rollback.

See `docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

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

## Stage 82 — durable PHYSIS fracture event API for TerraWeave host

PHYSIS now offers opt-in persistent `NatureFractureOutboxComponent`
and sorted pending-event/ack APIs, without any direct
Box3D/TerraWeave runtime dependency. The host maps a PHYSIS
Entity or grass cluster to its stable TerraWeave
`AnchoredPlantId` and valid `PlantFallEvent`, then calls
TerraWeave `FallenPlantPhysics::spawn` at its safe physics
barrier, reconciles/replicates the result and acknowledges
the event. The Terra implementation remains separate in
`feature/terraweave`; not merged here.
PHYSIS ECS ack and external float Box3D creation are not
atomic; native host owns replay reconciliation and
idempotence. Stage82 native test has **NOT RUN** yet.
See `docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`.
