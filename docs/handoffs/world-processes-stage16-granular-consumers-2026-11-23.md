# AIVERSE World Processes — Stage 16: shared read-only queries, full GRANULAR grid editor, 7-system example

**Date:** 2026-11-23
**Branch:** `feature/world-processes-experimental`
**Pull request:** https://github.com/chromealex/AIVERSE/pull/112 (draft)
**Main:** unchanged. The user explicitly requested feature development before testing.

## What was implemented

Seven additional genre-independent systems were built in Stages 11–15, augmenting ECHO, FRACTURE and AETHER: MORPH, CROWD, IMPRINT, CIRCUIT, GRANULAR, EVOLVE and SOCIETY. Each has separate authoritative ECS components, bounded integer update, opt-in `install(Simulation&)`, archive IO and versioned persistent component schemas. They use the existing simulation's World and tick, not a second physics/terrain/economy state.

### Stage 16 code and commit ledger

1. `libraries/include/aiverselib/world/WorldProcessesQueries.h` — commit `2154f05364cc59f706beb64ad7a05ff81c6df847`. Read-only copy/snapshot APIs:
   - `morphPose` / `morphPoses`: local deformations and velocities, sorted by packed Entity ID
   - `crowdPose`: quantized position and assigned slot
   - `imprintLevels`: footprint/scent/residue
   - `circuitState`: powered and signal output
   - `granularCell`: bounded per-cell mass/solid query
   - `evolution`: region durability/fertility/succession/pollution
   - `societyPerson` / `knowsTopic`: knowledge mask and trust-related participant state
   No World writes, new pools, persistent caches or renderer dependencies.

2. `libraries/include/aiverselib/world/CrowdEcs.h` — commit `855999d2427b0ac27111015698b3c8c452a00d8a`. Sorts preference components by full Entity ID and deduplicates chosen slots without losing the priority order.

3. `libraries/include/aiverselib/world/CircuitEcs.h` — commit `3cfc06141abaa9ae51caefc36bfefc5c6a68c351`. Refuses to drive a gate from stale/inactive input handles; in particular an invalid NOT source no longer acts like a valid false input.

4. `libraries/include/aiverselib/world/GranularDocument.h` — commit `d0acf1ff5a5951a29baddabb66f47820b877b13c`. Validated authored grid schema with `width`, `height`, `capacity`, `mass[]`, `solid[]`, `enabled`; bounded 1..128 per axis; explicit `create`, `validate`, `resize` (preserve overlap), `paint` (deposit/clear/obstacle) and `apply(World&, Entity, Json)` to actual ECS.

5. `libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h` — commit `1530c67a032c628b92f9a3b620bee4e6ce4a09d7`. Recognizes both previous simple 1×1 GRANULAR component authoring and full-grid documents without silently migrating old formats.

6. `apps/studio-native/StudioSession.cpp` — commit `4d7ddd15727992babbfd6cd241020de6297d019f`. Existing `studio.setProperty` accepts a bounded `componentId="GranularField", key="grid"` complete JSON payload; validates before one MapDoc undoable edit. Generic inspector disallows individual raw array mutations.

7. `apps/studio-web/src/components/GranularGridEditor.tsx` — commit `021fce4a0b648541f003797f6ffe02ee40f2a143`. A compact editor for up to 128×128 cell field with 16×16 sliding window, grain/wall/erase modes, amount input and size controls. Each edit submits one serialized validated document. `GranularGridEditor.css` — commit `1cffb1808c1a51fbd46121f6a72739a64aa661bb`; uses shared `--au-*` tokens.

8. `apps/studio-web/src/panels/InspectorPanel.tsx` — commit `e5b750bee5e9b28334f59696e5a9ed96efff59a8`. Reuses the existing Inspector component card, mounts the grid editor, routes changes to `onChange("grid",serialized)`, and hides raw dimensions/arrays to avoid inconsistent partial writes.

9. `examples/native/world_processes/authoring-examples.json` — commit `e7f57405a52cfb496b496410a1e094b3799875cc`: 23 linked map-authoring examples covering the seven systems. This is a **component data fragment**, NOT a standalone playable project: each real map entity still needs its game's valid type, owner, position and stable ID.
   `examples/native/world_processes/README.md` — commits `48da5311ffb70e1c30618eabea089b083d33721a` and `4f63a18c895a2c7d2c3edb30b1b49262fb87f44a`, documents native opt-in module and `rules.json` flags.

10. `skills/world/aiverse-world-processes/SKILL.md` — commit `2e8ace1e9d8cfed85392b7c06173c91fbe0b8e24`. Agent setup, APIs, ownership, feature limits and deferred acceptance updated.
    This document's commit: see `git log -1 -- docs/handoffs/world-processes-stage16-granular-consumers-2026-11-23.md`.

## Current architecture and honest feature coverage

| System | ECS functionality delivered | Other subsystem integration not yet delivered |
|---|---|---|
| ECHO | Authored zone/portal graph, emitter/listener hearing, durable ECS state | Full ray/material acoustics, audio mixer presentation |
| FRACTURE | Structural connectivity, latched collapse events, PHYSIS stress bridge | Durable Box3D debris creation and terrain fracture |
| AETHER | Quantized gas transport, sources/sinks and PHYSIS fire bridge | Wind/pressure CFD, rendered smoke |
| MORPH | Node/spring/impulse dynamics, shape memory and read-only poses | Mesh skinning, collision-aware FEM |
| CROWD | Slot assignment and bounded local avoidance | NavMesh/RVO/ORCA/animation integration |
| IMPRINT | Deposit/decay/diffusion of footprint/scent/residue | Physical contact producers, VFX/AI tracking consumption |
| CIRCUIT | Directed power graph and one-tick Boolean gates | Existing economy generators/loads and art presentation |
| GRANULAR | Bounded 2D granular field, deposition/excavation, grid brush | 3D particulate physics, TerraWeave terrain geometry conversion |
| EVOLVE | Regional long-term rates for durability, ecology and pollution | PHYSIS climate and terrain asset changes |
| SOCIETY | Trust-weighted one-hop knowledge/topic dissemination | NPC AI/dialogue/quest narrative adapters |

All ten mechanisms now have **source-level ECS cores** and opt-in integration paths. The seven originally missing ECS systems also have map authoring, linked stable-ID object references and read-only Live Inspector projections via previous Stage14–15 work. These mechanisms are gameplay simulation building blocks, not drop-in fully rendered production feature packages.

## Tests and QA — intentionally postponed

**No compile, CTest, sanitizers, native Mac build, browser typecheck or screenshot QA was run for Stage11–16.** This was requested by the user. GitHub acknowledging a source commit does not establish compile success or gameplay correctness. CI may be triggered automatically by commits; do not claim it passed without inspecting real results. The earlier Stage05 successful GCC/Clang/ASan focused CI predates Stage11–16 and does not verify them.

The feature branch remains unmerged and the draft PR is NOT ready to merge. The earlier Studio and World Processes feature branch should be reconciled with `main` and concurrent `feature/studio-html5` before full acceptance.

## Concrete next step for another agent

- Continue **feature-only** work as requested: review missing host/presentation integration contracts for the seven mechanisms, particularly geometry/animation adapters for MORPH, traffic/navigation goals for CROWD, world contact producer for IMPRINT, shared game economy drive for CIRCUIT, 2D-terrain data conversion for GRANULAR, PHYSIS climate pressure for EVOLVE and narrative events for SOCIETY. Use read-only query facade and existing owner APIs; do not create parallel physics/AI/terrain authority.
- Preserve Studio's chosen design system, MapDoc Undo/Redo, stable-ID object picker and Live read-only ECS separation.
- Only when the user opens the test phase: configure full CMake native/Studio builds, run targeted CTest and ASan/UBSan, replay/parity, comprehensive Mac `./run-tests.sh --quick`, and review full CI logs and real Studio captures. Fix failures without lowering assertions.
- Before any main merge: synchronize branches and reconcile conflicts, verify cross-platform tests, update progress table and handoff with exact test evidence.
