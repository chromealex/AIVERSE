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
`libraries/include/aiverselib/world/EchoEcs.h` is the reusable, opt-in gameplay hearing pass. Call `aiverse::echo::install(sim)` once inside the native `GameModule::setup`; create `AcousticZone`, `AcousticPortal`, `AcousticEmitter`, `AcousticReceiver`, and `AcousticHearing` components on entities. Source, zone and portal references use generation-safe ECS `Entity` handles. The system publishes authoritative `AcousticHearing`; do not create a parallel AI perception cache. A host can instead call `propagate(world,tick)` explicitly at its read/write barrier.

The native test is `ecs_echo_acoustics_test` (source `tests/ecs_echo_acoustics_test.cpp`). It covers COW rollback, save/reload, deterministic graph tie ordering, stale refs and bounded oversized inputs, but **has not been executed or validated on the current branch yet**. Run on a real build before calling the feature verified. ECHO currently models authored zone portals, not full audio ray tracing or reverb. See `docs/handoffs/world-processes-stage02-echo-2026-11-23.md`.

## Stage 03: FRACTURE structural ECS

Public headers: `aiverselib/world/FractureEcs.h` (always) and `aiverselib/world/FracturePhysisBridge.h` (optional PHYSIS integration). Use the **existing** PHYSIS nature stress/outbox and host's Box3D/TerraWeave physics; never recalculate wind/fire/quake stress or spawn physics inside FRACTURE.

In `GameModule::setup`, call `fracture::install(sim)` after any PHYSIS installer. Place `fracture::Member` on each structural part Entity, `fracture::Bond` on each link Entity; attach `fracture::Collapse` to parts whose physical detachment needs durable dispatch. Provide one or more anchored members. A damage-capability/authorized command changes `Member.broken` or `Bond.damage`; `fracture::advance(world,tick)` marks unsupported members detached and emits one pending collapse transition. Call `fracture::pending(world)`, stage/deduplicate the actual gameplay/Box3D/TerraWeave effect **durably**, then `fracture::acknowledge(world,event)`. Only a trusted repair should call `rearmAfterRepair` after the original event is acknowledged, original tick has passed, and bonds/physics are repaired.

PHYSIS hookup: call `addPhysisFractureBridgeSystem(sim)` **after** `physis.nature_stress` and **before** registering `fracture.support`. It reads `physis::NatureStressOutputComponent` and latches Member.broken; PHYSIS remains the source of environmental fracture facts. Do not attach a duplicate Collapse outbox to the PHYSIS primary fall event's body; let the PHYSIS durable outbox handle that object's falling reaction, and use Collapse for downstream structural members. The host reconciles the single physical-effects ledger.

Native regression: `ecs_fracture_structure_test` from `tests/ecs_fracture_structure_test.cpp` (CTest), including support cascade, damaged bonds, PHYSIS snapshot bridge, event ack/rearm, COW replay, save/load and stale Entity generation. Newly added code has **not passed a full native build or CI acceptance gate** yet; never mark ready just from presence of tests. Stage handoff: `docs/handoffs/world-processes-stage03-fracture-2026-11-23.md`.

## Stage 04: AETHER gas diffusion (experimental ECS integration)

Public interfaces: `aiverselib/world/AetherEcs.h`, optional `aiverselib/world/AetherPhysisBridge.h`. To enable, call `aiverse::aether::install(sim)` during native `GameModule::setup`, then attach `GasCell` to ECS room/volume/cell entities, `GasDuct` to ventilation/link entities and optional `GasFlux` to sources or sinks. These are **gas species only**: PHYSIS retains weather, climate, wind and fire. Equal-volume cells use integer-quantized masses of `smoke`, `toxin`, `oxygen`; a duct implements concentration-driven diffusion with a maximum mass transfer per tick, not a full computational-fluid-dynamics solver.

`aether::advance(world,tick)` stages all three species and fluxes before committing. The policy enforces signed rate/mass bounds and stable entity sorting, and refuses mixed partially-processed tick states. Call once per authoritative tick. State persists through registered `aether.gas_cell`, `aether.duct`, `aether.flux` v1 schemas.

To connect PHYSIS fire, call `aether::addPhysisFireSystem(sim)` after PHYSIS environment and after gas transport, during setup, when PHYSIS `State` and `HazardCell` exist. Attach `FireCoupling` to PHYSIS cell entities also containing `GasCell`. `applyPhysisFire(world)` reads current normalized fire intensity and creates smoke/consumes oxygen once per PHYSIS climate tick. It **does not extinguish or modify PHYSIS fire**; combustion chemistry, smoke light scattering and physical wind advection are not implemented. `FireCoupling` is a persistent opt-in `aether.fire_coupling` v1 component; do not register twice.

Acceptance source `tests/ecs_aether_gas_test.cpp` / CTest `ecs_aether_gas_test`: conservation, deterministic cross-module hashes, invalid flux fail-closed, duplicate tick, stale generations, PHYSIS coupling, COW save/replay. Tests were authored, **not verified as compiled/passed** in this session. Handoff: `docs/handoffs/world-processes-stage04-aether-2026-11-23.md`. Follow the five-area WIP limit and do not enable 4th+ system before closing current ECHO/FRACTURE/AETHER gates.
