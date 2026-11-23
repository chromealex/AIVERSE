# World processes: FRACTURE / AETHER / MORPH / ECHO / CROWD / IMPRINT / CIRCUIT / GRANULAR / EVOLVE / SOCIETY

Status: **experimental pure-policy probes only**. Nothing here is a finished engine subsystem, an authoritative ECS system, or editor capability. This stage does not add a second mutable world.

## Stage 1: code available

- Public C++20 header: `libraries/include/aiverselib/world/WorldProcesses.h`
- Standalone smoke tests: `tests/libraries/world_processes_test.cpp`
- Runner: `bash tests/libraries/world_processes_run.sh`; uses `CXX` when provided
- Linkage: existing header-only `AIVERSE::libraries` (its public includes already include `libraries/include`).
- No changes to legacy modules, no engine registration, no genre feature flags.

| System | Stage 1 kernel | Required engine work |
| --- | --- | --- |
| FRACTURE | graph connected-to-anchors classification | strength/load solver, bond fracture, Box3D integration, debris authority |
| AETHER | deterministic mass transfer over directed flows | 3D air volumes, wind, fire/oxygen, ventilation, sparse scheduling |
| MORPH | integer spring step toward rest pose | constraints, softbody solver, collision, visual mesh deformation |
| ECHO | loss-based acoustic route propagation | occlusion/portals, reflections, audio engine & AI perception |
| CROWD | stable unique space-slot allocation | steering/RVO composition, congestion, panic/evacuation, LOD |
| IMPRINT | bounded track-depth weathering | terrain decals/deform and PHYSIS weather bindings |
| CIRCUIT | directed boolean reachability | reuse existing electrical/power networks; switches, overload, cycle policy |
| GRANULAR | bounded 2D gravity pass | sparse chunks, obstacles, pressure, terrain and water interactions |
| EVOLVE | deterministic tick-based durability ageing | use existing ecology/material/damage owner, persistence and world LOD |
| SOCIETY | one-hop knowledge spread from tick-start snapshot | reuse narrative relationships, facts, social perception, social rules |

The API uses fixed-width integer quantities but **does not establish numerical scaling or save schema**. Inputs such as graphs/flows must be authored and ordered consistently for deterministic replays. The output of the pure-policy functions must be committed through the ECS writer at an explicit barrier; do not store a second authoritative vector.

## Integration boundaries

- Prefer existing owners: PHYSIS for weather/fire, TerraWeave for terrain, NEREID for water, Box3D for physics, narrative-graphs for knowledge/relationships, existing energy and interaction systems for circuits.
- ECS components (data only) must own per-entity authoritative process parameters and persistent state. Register with `Simulation::registerPersistentComponent<T>` using portable `io`, stable namespace/schema version and checksum/snapshot tests.
- Explicit stages: gather read-only environment snapshots -> run bounded deterministic policies -> canonical events/commands -> ECS structural barrier -> presentation.
- Reuse shared pathways for sound/AI, physics, material and terrain interactions. No genre branches.
- Do not hook all ten into the scheduler at once. Respect `AGENTS.md` maximum five areas in progress, and prefer closing current partial registry work before enabling new areas.

## Actual verification

A local **equivalent implementation** of all ten policies compiled and executed with `c++ -std=c++20 -Wall -Wextra -Werror -pedantic` on 2026-11-23. The checked-out GitHub branch itself was **not built or tested** in the current environment; do not label it green. The remote test source and runner are committed but unexecuted in repository CI. The local standalone test checked ten happy-path cases and several invalid inputs, not full determinism, memory safety or native game integration.

## Handoff

Branch: `feature/world-processes-experimental`. Initial code commit `242e8b320c41b3d8d11f31046a103bf7128b5ef9`, tests `0336705970419fed2720f5061a1da40f361e1a17`, standalone runner `65e612b2a2b8a61587951a269e8743e79d11f54a`. Document commit SHA is available in Git history.

Next steps, in order:
1. On Mac, run `bash tests/libraries/world_processes_run.sh`; investigate compiler/sanitizer problems. Add a CTest target using the existing library-test patterns, and verify on merged tree before any main integration.
2. Audit existing ECS ownership/registry and identify a single primary mechanic to fully implement. Start with CIRCUIT only if existing electrical graph doesn't already cover its intended functions; otherwise choose ECHO or FRACTURE.
3. Add one data-only ECS schema, persistent component registration, authoritative system, safe event barrier, two native consumers, replay/hash/rollback tests, and field metadata for HTML5 Studio.
4. For each completed stage update handoff with tests actually run, branch, SHA, changed files, limits and next action. Add web demo and real capture when visually meaningful, following AGENTS.md.
5. Only expand other items after keeping within the five-area WIP limit. Do not merge to main without a green merged-tree gate.

This is a foundation for staged work on all ten requests, **not** a claim that those ten systems are implemented.
