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
