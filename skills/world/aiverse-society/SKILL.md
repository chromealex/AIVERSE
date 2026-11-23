---
name: aiverse-society
description: Use AIVERSE SOCIETY when implementing Распространение знанийслухов по доверительным связям и события NPC для существующих AI, диалогов и квестов.
---
# SOCIETY — AIVERSE skill

## When to activate
Распространение знаний/слухов по доверительным связям и события NPC для существующих AI, диалогов и квестов.

Typical request: память NPC, информация о событиях, доверие, слухи, реакции квестов и поведений. Use this skill only for this system; for cross-system configuration consult `skills/world/aiverse-world-processes/SKILL.md`.

## Canonical code and API
- **Gameplay headers:** `libraries/include/aiverselib/world/SocietyEcs.h`, `libraries/include/aiverselib/world/SocietyNpcTriggers.h`.
- **Authoritative ECS:** `society::Person, Contact, Message, NpcTrigger`.
- **Lifecycle:** `aiverse::lib::worldprocess::installSuite(sim,flags) with flags.society=true`; per-tick behavior: `society.knowledge -> society.npc_reactions`. Install once in the authoritative `Simulation::setup` phase, before the persistence registry seals; don't install the same system twice via suite and direct calls.
- **Studio-authored components:** `SocietyPerson, SocietyContact, SocietyMessage, SocietyNpcTrigger`. These are map authoring fields, not a writable duplicate of live ECS output.
- **Adjacent adapters:** `examples/world/world_processes_native/SocietyNpcHost.h`, `WorldProcessGameplayHost.h`, `WorldProcessesExtendedLinks.h`. Native project integrations are optional; include only in the target that already owns those subsystems.

## Minimal C++20 usage pattern
```cpp
#include "aiverselib/world/SocietyEcs.h"
aiverse::lib::worldprocess::SuiteFlags flags;
flags.society=true;
aiverse::lib::worldprocess::installSuite(sim,flags);
// Configure an NpcTrigger against Person and a nonzero knowledge topic.
// Native SocietyNpcHost dispatches project actionId to existing AI/quest.
```

If using normal GameCore map projects instead of a custom module, prefer the compiled opt-in `world_processes` native module and Studio. Example fragments: `examples/native/world_processes/authoring-examples.json`; native setup/lifecycle: `examples/native/world_processes/README.md`.

## Do not cross these ownership boundaries
Never duplicate quest graph, dialogue manager or NPC AI blackboard. 64-bit topic mask is a compact mechanic, not a full language model/memory store. Host must deduplicate authoritative actions.

## Acceptance checklist (execute when user lifts test deferral)
- Directed trust threshold, one-hop-per-tick, one-shot vs repeating triggers, actionId ownership, event idempotence after rollback, case-sensitive stable refs.
- Verify C++ compilation, CTest source and actual run outcome; include GCC/Clang, ASan/UBSan where supported.
- Check authoritative tick order, World snapshot/COW, persistent schema IDs, clean reload, rollback/replay checksums, malformed input rejection, and expired Entity generation references.
- If the feature is visually observable, capture **real** native Studio/game screenshots with source SHA and platform/backend provenance; do not use fabricated renders as QA proof.
- Stage13/24–25 NPC behavior, replay and Studio tests not executed.

## Handoffs and current verification
- Read matching `docs/handoffs/world-processes-stage13-granular-evolve-society-2026-11-23.md`, `docs/handoffs/world-processes-stage24-society-npc-actions-2026-11-23.md`, `docs/handoffs/world-processes-stage25-gameplay-host-2026-11-23.md` (some stage handoff names contain extra suffixes; search `docs/handoffs/world-processes-stageNN-*` when needed).
- The PR is **DRAFT**; latest source changes have **not** been verified by a completed full native build or end-to-end scene test. Do not infer production readiness from the presence of API/docs.

## Cross-cutting contract
- Read `AGENTS.md`, `docs/agents/ecs.md`, `docs/ecs-mechanic-composition.md`, and `skills/world/aiverse-world-processes/SKILL.md` only as necessary for system boundaries.
- All live game state must be ECS-owned: `World` + `Simulation`. C++20, stable packed-generation `Entity` links in memory; map authoring stores only stable document ID strings. Never serialize ephemeral ECS slot numbers.
- Integration is opt-in. For regular GameCore maps, compile `examples/native/world_processes/WorldProcessesModule.cpp`, enable `{"id":"world_processes"}` in the project's `rules.json` `nativeModules`, and use the existing `StudioSession` map component authoring. Actual adapter: `aiverselib/world/WorldProcessesNativeModule.h` consumes `GameCore::matchMap()` + `mapPlaced()`.
- Shared standalone/linked map schemas: `aiverselib/world/WorldProcessesAuthoring.h`, `WorldProcessesLinkedAuthoring.h`, `WorldProcessesExtendedAuthoring.h`, `WorldProcessesExtendedLinks.h`. `WorldProcessesMapHydration.h` resolves live ECS owners with strict validation before installing/creating components.
- In Studio use existing Add Component, reference picker, Scene gizmos and native `MapDoc` Undo/Redo. For live observation use `WorldProcessesInspector.h`/`WorldProcessesExtendedInspector.h`, never writable runtime clones.
- After each implementation step add a new `docs/handoffs/world-processes-stageNN-*.md` describing code paths, API, ownership, branch/commit, actual tests/results, missing features and exact next action. Update agent guidance; do not merge unverified code into main.
- Current source is in draft PR #112 on `feature/world-processes-experimental`. New Stage11+ features are **source-only, not confirmed compiling or passing runtime tests**. The user deferred testing; do not describe old Stage05 successful tests as proof of these newer files.
