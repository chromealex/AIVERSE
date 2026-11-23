# Stage 02 — ECHO ECS zone/portal hearing (2026-11-23)

## Branch and scope
- Branch: `feature/world-processes-experimental` (based on main at Stage 01).
- Main implementation commit: `c790119cd9f29b18daf49e4aa50bc4c5f1f62ba0`.
- Native regression source: `e8ac66e057be865cb7c622c8eba5cfa0f6015150`.
- CTest registration: `20b82321eb1dda42e5feb164d50c1493b690891f`.
- Release-safe Stage 01 test corrections: `2161beb4a8593f791df0e3ac23deeaa578d6624e`, `4ebaa2e6cb7ea29e41868082ece4463d6e5a66b8`.
- Documentation commit SHA is the commit containing this file; see Git history.
- Other 9 systems still have Stage 01 experimental pure policies only. No claim of production readiness.

## Exact changes and API
- `libraries/include/aiverselib/world/EchoEcs.h` adds data-only ECS components:
  - `echo::AcousticZone`: attach to a zone Entity.
  - `echo::AcousticPortal`: Entity refs `from/to`, integer attenuation, optional reverse direction, enable flag.
  - `echo::AcousticEmitter`: owner Entity, referenced active zone, positive integer intensity, enable flag.
  - `echo::AcousticReceiver`: owner Entity, referenced active zone, integer threshold, enable flag.
  - `echo::AcousticHearing`: authoritative latest source Entity, intensity, tick, audible/valid.
- ADL portable `io(Archive&, T&)` for all five component types; limited integer ranges fail on read. Stable native schema IDs: `echo.zone`, `echo.portal`, `echo.emitter`, `echo.receiver`, `echo.hearing` at v1.
- `echo::install(Simulation&, name="echo.propagation")` registers persistence and exactly one system during `GameModule::setup`; the game creates/configures entities itself.
- `echo::propagate(World&, tick)` is the direct deterministic step for native hosts/tests. Read set: zones, portals, emitters, receivers; write set: hearing **only**. No structural changes or gameplay events.
- All graph references are generation-safe ECS `Entity` handles. Active zones sorted by packed ID; portals and emitters sorted by packed entity ID. Loudest audible source wins with smaller source ID on ties, independent of sparse-set order.
- It reuses Stage 01 `lib::worldprocess::audibility` integer shortest-loss graph solver, retaining a single source of graph policy.
- Limits: 4096 zones, 16384 portal components, 256 emitters; overflow returns `false` and clears hearing outputs as invalid for the tick. Invalid/stale links are ignored.
- No generic audio emitter is replaced. Audio playback, spatial HRTF, full material reflections and reverb are presentation/authoring follow-ups, not simulated by this step.

## Minimal native setup
```cpp
#include "aiverselib/world/EchoEcs.h"
void Game::setup(aiverse::Simulation& sim, const aiverse::Json&, const aiverse::Json&) {
    aiverse::echo::install(sim);
    auto& world=sim.world();
    const auto room1=world.create();world.add<aiverse::echo::AcousticZone>(room1);
    const auto room2=world.create();world.add<aiverse::echo::AcousticZone>(room2);
    const auto link=world.create();
    world.add<aiverse::echo::AcousticPortal>(link,
        aiverse::echo::AcousticPortal{room1,room2,25,true,true});
    const auto emitter=world.create();
    world.add<aiverse::echo::AcousticEmitter>(emitter,\n        aiverse::echo::AcousticEmitter{room1,100,true});
    const auto listener=world.create();
    world.add<aiverse::echo::AcousticReceiver>(listener,\n        aiverse::echo::AcousticReceiver{room2,30,true});
    world.add<aiverse::echo::AcousticHearing>(listener);
}
```
Consumers read the hearing component after the registered ECHO pass. Do not hash persisted native component fields a second time in `GameModule::hashState`.

## Regression cases authored (execution NOT VERIFIED)
- `tests/ecs_echo_acoustics_test.cpp`: two independent GameModule template types, same source, 2 portals and 3 zones (100 - 25 - 20 = 55), serial and COW modes, snapshots, disk save/load, peer checksum, stale-generation reference, portal closure, exact tie/source ordering, threshold, oversized graph fail-closed.
- CTest: `ecs_echo_acoustics_test` via `aiverse_ecs_echo_acoustics_test` and `AIVERSE::libraries`, `aiverse_engine`.
- The previous Stage 01 pure tests now use `AIVERSE_TEST_CHECK`, so they cannot silently pass under `NDEBUG`.

## Verification and limitations
- GitHub accepted code, test and CMake commits. **No native compile, CTest, sanitizer, macOS, CI, Studio web, or full project build has been executed on the new branch in this session.**
- This is **implemented but unverified** ECHO zone-portal gameplay hearing (not full physical acoustic ray/path simulation).
- No sound rendering/audio API integration, material response, occlusion geometry extraction, AI perception feedback, dynamic portal authoring in Studio, or demo. Inputs require the host to choose and assign zone IDs/portal links; invalid refs fail closed.
- Existing platform tests, `./run-tests.sh --quick` on the merged Mac tree and replay parity are mandatory before merge. No code was merged into main.

## Exact next agent step
1. On Mac checkout branch and run `cmake --build <build> --target aiverse_ecs_echo_acoustics_test`; `ctest --test-dir <build> -R '^ecs_echo_acoustics_test$' --output-on-failure`, then the standalone `bash tests/libraries/world_processes_run.sh`.
2. Correct compiler/runtime failures; add explicit `CMAKE` and header compile checks where affected and run sanitizer target. Handoff every stage including actual test output and commit.
3. Add generic editor component field metadata and a tiny 2-room/3-room demo with authorable portals, sound source and visualized reception levels. Link to AI perception via a typed read interface, not a parallel sensing state.
4. Integrate occlusion and sound zone mapping from world geometry and dynamic doors while preserving ECS read/write barriers and replay hashes.
5. After ECHO reaches verified core, begin the next chosen area (FRACTURE if PHYSIS damage owner can be reused), respecting the <=5 WIP rule; the remaining eight policies are only prototypes.

## CI architecture correction
The initial PR CI layer check failed on the ECHO engine -> library include edge. Corrected by adding library-layer `EchoEcs.h` (commit `90b4c6cc1b1545a4e846b9b59cb34660cd97c947`), updating the consumer (commit `62a6460f0d2b5578186d69501bb3f4db5266bf0b`), and deleting the offending engine header (commit `c060341afa9286cc6921e712c250e23a4029d4f7`). The same CI log lists 13 independent `engine/voxel` layer violations, present in the merge checkout and not introduced by this ECHO change; record as an external blocking owner rather than marking the layer test green. The post-correction CI has not yet been confirmed.
