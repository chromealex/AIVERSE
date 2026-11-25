# AIVERSE world processes — stage 03, FRACTURE structural ECS
Date: 2026-11-23
Branch: `feature/world-processes-experimental`
Draft PR: https://github.com/chromealex/AIVERSE/pull/112
Main: NOT merged. Two work areas opened here (ECHO and FRACTURE); respect AGENTS.md max-five WIP and existing partial-area priority rules.

## What was actually added
- `libraries/include/aiverselib/world/FractureEcs.h`:
  - optional `Member` on each structural entity, with authored anchor and authoritative broken/detached latch;
  - optional `Bond` on each joint entity with generation-safe endpoints, integer strength/damage;
  - optional `Collapse` persisted as a separate *support-loss* event (Empty/Pending/Acknowledged), with deterministic entity/tick key;
  - sorted connected-support search based on the previously introduced `worldprocess::supported` pure kernel;
  - failure budgets: at most 4096 member components and 16384 bond components;
  - `advance(World&,tick)` reads component snapshots and writes only newly-detached Member/Collapse state;
  - `pending(const World&)` sorted by packed Entity ID, `acknowledge(World&,Event)` rejects wrong key, tick, generation or duplicates, and `rearmAfterRepair(World&,Event,currentTick)` for trusted, later-tick repair.
  - `install(Simulation&,name)` registers portable native persistence schemas `fracture.member`, `fracture.bond`, `fracture.collapse` v1 and an opt-in system.
- `libraries/include/aiverselib/world/FracturePhysisBridge.h`:
  - `applyPhysisFractures(World&)` reads existing `physis::NatureStressOutputComponent` only; latches corresponding structural Member broken;
  - `addPhysisFractureBridgeSystem` for installation after PHYSIS stress and before FRACTURE graph pass;
  - does not write PHYSIS outbox or recreate climate/forces, Box3D bodies or terrain edits.
- `tests/ecs_fracture_structure_test.cpp`: authored native double GameModule test, support-chain cascade, bond damage, stable-order events and ack, retry/repair, source stale Entity generation, size budget, COW peer checksum, save/load and snapshot rollback; synthetic PHYSIS stress adapter.
- Root `CMakeLists.txt`: new `aiverse_ecs_fracture_structure_test` and CTest name `ecs_fracture_structure_test`.
- Agent skill `skills/world/aiverse-world-processes/SKILL.md` expanded to cover the exact usage and ownership boundaries.

## Commit ledger
- `b55209c6aae74bdaec8321bf0ccd307a8a71462f`: FRACTURE ECS members, bonds, collapse outbox.
- `a5629a7129b00390e3829f913ac911a25aae5985`: native ECS test.
- `5477d59811547483b0c0c708db85c95cf53273d6`: CTest registration.
- `9f183ff0fb48e8fd24a6f8732be545cc7043d3d2`: optional PHYSIS bridge.
- `a5b77607d813511ffcf63604ff9e39ccb01fb306`: stress bridge test.
- `e0a7487e27c9f256de5d753c11da20e2eae06592`: trusted rearm API.
- `5b9cf868897257dd1ecf7c7f59ac2334d20d62a5`: repair/rearm tests.
- `c310c3fd2e8c59e3e73ea46fdff33390f7040909`: updated agent skill.
- Handoff commit: retrieve via `git log -1 -- docs/handoffs/world-processes-stage03-fracture-2026-11-23.md`.

## Native consumer example
```cpp
#include "aiverselib/world/FractureEcs.h"
#include "aiverselib/world/FracturePhysisBridge.h"
// During GameModule::setup, AFTER PHYSIS install:
// If PHYSIS enabled: addPhysisFractureBridgeSystem(sim);
aiverse::fracture::addPhysisFractureBridgeSystem(sim);
aiverse::fracture::install(sim);
auto& w=sim.world();
auto foundation=w.create(),wall=w.create(),joint=w.create();
w.add<aiverse::fracture::Member>(foundation,
    aiverse::fracture::Member{true,false,false});
w.add<aiverse::fracture::Member>(wall);
w.add<aiverse::fracture::Bond>(joint,
    aiverse::fracture::Bond{foundation,wall,100,0,true});
w.add<aiverse::fracture::Collapse>(wall);
// After a trusted damage command (not renderer):
w.get<aiverse::fracture::Bond>(joint).damage=100;
// On subsequent authoritative tick, unsupported wall becomes detached.
// At host commit barrier, inspect fracture::pending(w) and durably stage.
// Call fracture::acknowledge(w,event) after committing physical effects.
```
The PHYSIS bridge in this example is optional and must be OMITTED in a project without PHYSIS. A host that already uses PHYSIS outbox for the primary fallen object should NOT create a second Collapse event on that same member: let PHYSIS own the primary fracture, FRACTURE own distinct unsupported descendants. Use one durable external effects ledger and reconciled replay on physics rollback.

## Validation status (truthful)
- GitHub accepted all above source/CTest/docs commits.
- This stage's FRACTURE tests were **written but not executed or verified** on a native toolchain or CI. No claim of passing Linux/macOS/Windows or sanitizers.
- Older PR #112 merge-CI architecture step reported an ECHO engine->library violation, fixed by moving ECHO from engine/ to `libraries/include/aiverselib/world/EchoEcs.h`. The subsequent merge-CI at run 45201746772 reported **13 remaining engine/voxel -> libraries violations** and **no ECHO violation**. These are pre-existing/parallel files outside this stage, not a green architecture gate.
- The earlier global meta/library CI also failed on `GameCore.h` missing `override` warnings treated as errors, independent of FRACTURE. Workflow cancellation on new pushes is not test success. Last FRACTURE-specific CI result: **pending/unverified**.
- No HTML5 Studio field metadata, working gameplay scene, actual Box3D collapse, TerraWeave structural debris, live VFX, or downloadable demo. Pure graph connectivity is not a finite-element or stress propagation solver.

## Exact next step
1. Stop committing until the latest PR CI revision has a chance to run; inspect first build diagnostic and fix any FRACTURE/ECHO regressions, documenting actual outputs (not assumed).
2. On Mac: `git fetch origin && git switch feature/world-processes-experimental && git pull --ff-only` then configure an AIVERSE test build and compile the `aiverse_ecs_echo_acoustics_test` and `aiverse_ecs_fracture_structure_test` targets. Run `ctest --test-dir <build> -R '^ecs_(echo_acoustics|fracture_structure)_test$' --output-on-failure` and standalone `bash tests/libraries/world_processes_run.sh`.
3. Run COW deep/serialized replay, ASan/UBSan, invalid-save and cross-worker determinism, plus `./run-tests.sh --quick` on the merged Mac tree, as AGENTS.md requires before main.
4. Bind authoring and editing of zones/portals and structure/bonds into the generic HTML5 Studio inspector; use existing typed ECS metadata instead of a new editor backend. Record actual video/screenshots and micro web demo when functional.
5. Integrate a real host's durable structural event sink using existing Box3D and TerraWeave fall ledgers and explicitly test reconciliation after rollback, without double-delivering PHYSIS primary outbox events.
6. After ECHO and FRACTURE verify as core and existing partial areas are addressed, continue remaining eight systems one at a time; their Stage01 policy headers are not production systems.
