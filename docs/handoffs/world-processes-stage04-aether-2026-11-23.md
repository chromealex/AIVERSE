# AIVERSE — Stage 04: AETHER ECS gas transport and PHYSIS fire coupling

Date: 2026-11-23
Branch: `feature/world-processes-experimental`
Draft PR: https://github.com/chromealex/AIVERSE/pull/112
Main: unchanged. Work-in-progress: ECHO, FRACTURE, AETHER. Remaining 7 areas have only Stage01 policy probes; not production-ready.

## Ownership, what this step does
- PHYSIS is still the sole owner of wind, climate, hazard fire/fuel and its tick clock. NEREID owns water; TerraWeave owns terrain; SpectraFX owns VFX. This step introduces no new weather/fire simulation, gas renderer, or game-genre fork.
- AETHER owns integer-quantized **gas species mass** only (smoke, toxin, oxygen) on opt-in world ECS entities. Cells are assumed equal-volume; concentration differences drive graph transport through authored duct edges. This is neither mass advection from wind, 3D Navier-Stokes, combustion chemistry, nor full physical ventilation.
- The canonical live values stay inside ECS `GasCell` components; no parallel simulation singleton, global grid or read/write cache. Scratch arrays exist within one authoritative update then are discarded.

## Changed files and runtime API
1. `libraries/include/aiverselib/world/AetherEcs.h`:
   - `GasCell{smoke,toxin,oxygen,sampledTick,enabled}` on existing room/volume/PHYSIS-cell entities.
   - `GasDuct{from,to,maximumPerTick,oneWay,enabled}` on conduit entities (generation-safe Entity references).
   - `GasFlux{cell,smoke,toxin,oxygen,enabled}` optional per-tick source/sink on any Entity.
   - `advance(World&, uint32_t tick)`: deterministic, bounded, staged multi-species diffusion and flux; all cell values atomically committed only if all checks pass. Stable sorted Entity processing independent of sparse-set insertion order. One-way valves restrict direction; zero/negative concentration gradients don't transfer mass. All species remain nonnegative; active sources can add or subtract a bounded amount.
   - `registerPersistence(sim)` and `install(sim,name="aether.transport")`: opt-in v1 portable component schemas `aether.gas_cell`, `aether.duct`, `aether.flux`, plus one tick system. No change to old save manifests when disabled.
   - Caps: at most 8192 GasCells, 32768 GasDucts and 8192 GasFlux components; each species 0..1e9; per-edge/per-source rate <=1e7. Invalid present gas, active duct/flux or impossible source consumption fails before any cell mutation. Duplicate invocation for a fully processed tick does nothing; mixed processed/unprocessed state fails closed.
2. `libraries/include/aiverselib/world/AetherPhysisBridge.h`:
   - `FireCoupling{oxygenConsumedAtFullFire,smokeProducedAtFullFire,processedClimateTick,enabled}` optional per-cell ECS component.
   - `applyPhysisFire(World&)`: reads `physis::State.tick` and `physis::HazardCell.fire` on the same authoritative Entity, consumes available oxygen and emits bounded smoke once per PHYSIS climate tick. Does not touch PHYSIS fields, extinguish fire, or create new hazard ownership.
   - `addPhysisFireSystem(sim,name)`: opt-in persistent v1 schema `aether.fire_coupling`; host schedules after PHYSIS environment and AETHER transport.
3. `tests/ecs_aether_gas_test.cpp`: authored native GameModule x2 cases, exact mass conservation with a 100-unit duct, three species, production flux, same-tick idempotence, COW replay/peer checksums, saved v10 load and corrupt-load rejection, invalid configuration fail-closed, stale generation non-reconnection, over-budget rejection, synthetic PHYSIS fire samples including duplicate sample prevention.
4. `CMakeLists.txt`: new target `aiverse_ecs_aether_gas_test`, CTest `ecs_aether_gas_test`, linking `aiverse_engine AIVERSE::libraries`.
5. `skills/world/aiverse-world-processes/SKILL.md`: Stage04 integration recipe.
6. Previous FRACTURE compile-source audit fix: unqualified `mix64` (only defined in `aiverse::physis`) replaced with `Hasher` in `libraries/include/aiverselib/world/FractureEcs.h`, preserving deterministic event identity without any PHYSIS dependency.

## Commits
- `7515c4f444ddc7f314c13229d1704f210159884f`: FRACTURE event key compile fix using core Hasher.
- `86c4b16d4ea00f0960733f723720797faaf6b0e9`: AETHER ECS code.
- `9f40643d9fffd9eb49c3e0ed482bed23fb477a5e`: AETHER/PHYSIS opt-in fire adapter.
- `4612af494ab97eefd510f46b957c469b080b927d`: regression test source.
- `0dfa2a4ee2308804751fb78045fe59264c8b32ef`: CTest registration.
- `3815e11e87a6cd509421bd6c8da2a6844b7e2bc9`: agent recipe.
- Handoff itself: `git log -1 -- docs/handoffs/world-processes-stage04-aether-2026-11-23.md`.

## Verification — what actually happened
- GitHub accepted source and test commits. No local checkout of the full AIVERSE repository was available to compile Stage04. **New AETHER, FRACTURE and ECHO native CTest cases are not yet confirmed passing.**
- Last checked PR #112 merge CI `45203847450` (prior Stage03 commit) was queued with Windows MSVC entering progress; intermediate pushes cancel pending jobs. This is not evidence of successful compilation.
- Prior CI on SHA `72d2a6f...`: layer checker found **13 engine/voxel -> libraries violations**, no ECHO violation after moving ECHO into the library layer. This is a separate owner/debt. Earlier library suite surfaced `GameCore.h` override warnings, also unrelated to new AETHER files.
- No ASan/UBSan, cross-platform capture, HTML5 Studio inspector, Web demo, PHYSIS gas field performance measurement, real-game integration or passed Mac quick gate. No merge to main.

## Native consumer contract
```cpp
#include "aiverselib/world/AetherEcs.h"
#include "aiverselib/world/AetherPhysisBridge.h"
// During GameModule::setup, after optional PHYSIS environment registration:
aiverse::aether::install(sim);
auto& w=sim.world();
auto roomA=w.create(), roomB=w.create(), duct=w.create();
w.add<aiverse::aether::GasCell>(roomA,
    aiverse::aether::GasCell{300,0,100});
w.add<aiverse::aether::GasCell>(roomB,
    aiverse::aether::GasCell{0,0,200});
w.add<aiverse::aether::GasDuct>(duct,
    aiverse::aether::GasDuct{roomA,roomB,100,false,true});
// A GasFlux is optional. Calling addPhysisFireSystem(sim) is ALSO OPTIONAL,
// only when PHYSIS is installed and on cells with HazardCell and FireCoupling.
```
If PHYSIS is installed, order systems: `physis.environment` -> `aether.transport` -> `aether.physis_fire`, with all other PHYSIS stages in the canonical documented order. Build PHYSIS's map/hazard ownership using its established installer; do NOT synthesize another Atmosphere.

## Limitations and next exact steps
1. **Before starting a fourth ECS area**, inspect latest PR #112 CI, compile all three targets `aiverse_ecs_echo_acoustics_test`, `aiverse_ecs_fracture_structure_test`, `aiverse_ecs_aether_gas_test`; run `ctest --test-dir <build> -R '^ecs_(echo_acoustics|fracture_structure|aether_gas)_test$' --output-on-failure`. Fix source/test errors without weakening assertions.
2. Run `bash tests/libraries/world_processes_run.sh`, sanitizers, deterministic worker/rollback and native v10 persistence. On actual Mac merged tree run `./run-tests.sh --quick` before any main integration. Update the handoff with exact commit SHA, test counts and failures.
3. Integrate PHYSIS wind into explicit conduit transport/advection using read-only `physis::windAt` and deterministic volume/capacity units (not implemented), calibrate fire oxygen coupling against PHYSIS fuel/fire, implement outdoor dispersion and coalescing sparse cells.
4. Implement native authoring/Inspector tooling in the HTML5 Studio branch, set up small demonstration scenes and a web demo + real captures when visually usable.
5. Keep ECHO, FRACTURE and AETHER distinct in simulation data ownership; do not add overlapping area work while existing WIP/partial registry is over limit.

This stage implements a bounded integer ECS gas transport slice, NOT a production-ready physically correct gas solver.
