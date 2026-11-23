---
name: aiverse-world-processes
description: Use experimental deterministic world-process policies, then integrate them into a single ECS authority without duplicating PHYSIS, TerraWeave, NEREID, physics, power grids or narrative state.
---
# World processes integration recipe

Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `docs/handoffs/world-processes-stage01-2026-11-23.md`.

Ten policy experiments live in `libraries/include/aiverselib/world/WorldProcesses.h`:
- `supported` (FRACTURE), `diffuse` (AETHER), `relax` (MORPH), `audibility` (ECHO), `assignSlots` (CROWD),
  `erode` (IMPRINT), `energize` (CIRCUIT), `settle` (GRANULAR), `age` (EVOLVE), `shareKnowledge` (SOCIETY).

These functions do not touch `Simulation`. They are intentionally NOT self-contained game mechanics. Integrate only the chosen process:
1. Search the owners' existing components and source first; extend shared engine capability rather than reimplementing it.
2. Define data-only component contracts, deterministic unit/scale, validation, stable schema ID, `io` persistence, explicit read/write stage and event barrier.
3. Use integer/fixed authoritative state only. Do not write hidden mutable arrays or genre modes. Keep presentation decoupled.
4. Implement native consumers and verify test coverage for deterministic ordering, invalid indexes, boundaries, save/load, rollback/replay and different worker counts.
5. For anything seen in the editor, add generic component metadata and a minimal website demo with genuine frames when it becomes a usable capability.
6. Document actual verification; never claim the provisional pure-policy probes are shipped ECS features.
7. At the end of each stage, update the handoff (branch, SHA, changed files, API, tests run, limitations and exact next action). Respect the five-area WIP limit and main-merge gate.

Smoke test: `bash tests/libraries/world_processes_run.sh`.


## Stage 02: ECHO's first ECS integration
`engine/world/echo/EchoAcoustics.h` is the reusable, opt-in gameplay hearing pass. Call `aiverse::echo::install(sim)` once inside the native `GameModule::setup`; create `AcousticZone`, `AcousticPortal`, `AcousticEmitter`, `AcousticReceiver`, and `AcousticHearing` components on entities. Source, zone and portal references use generation-safe ECS `Entity` handles. The system publishes authoritative `AcousticHearing`; do not create a parallel AI perception cache. A host can instead call `propagate(world,tick)` explicitly at its read/write barrier.

The native test is `ecs_echo_acoustics_test` (source `tests/ecs_echo_acoustics_test.cpp`). It covers COW rollback, save/reload, deterministic graph tie ordering, stale refs and bounded oversized inputs, but **has not been executed or validated on the current branch yet**. Run on a real build before calling the feature verified. ECHO currently models authored zone portals, not full audio ray tracing or reverb. See `docs/handoffs/world-processes-stage02-echo-2026-11-23.md`.
