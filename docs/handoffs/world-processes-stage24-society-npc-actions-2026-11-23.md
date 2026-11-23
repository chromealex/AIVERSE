# AIVERSE World Processes — Stage 24: SOCIETY NPC behavior and quest events

Date: 2026-11-23. Repo chromealex/AIVERSE, branch `feature/world-processes-experimental`, PR #112 DRAFT; no main merge.
User instruction: continue functions, postpone tests.

## Features added

1. `libraries/include/aiverselib/world/SocietyNpcTriggers.h`, commit `c524147b42d906965cc68f52281f28856b532e69`.
   - `society::NpcTrigger`: canonical persistent ECS component pointing to real `society::Person`, a required 64-bit topic mask, nonzero game-defined actionId, condition `TriggerMode::{AllTopics,AnyTopic,NoTopics}`, enabled/once and authoritative active/fired/transition/changedTick/sampledTick.
   - `society::updateNpcTriggers(World&,tick)` evaluates each trigger after SOCIETY one-hop dissemination, records one-shot outcomes and transition counters in authoritative World. Trigger IDs are actual full-generation Entities, not Python/JS slot indexes. Save/replay/COW shares the same ECS state; the host need not query dialogue or global NPC state to determine knowledge.
   - `society::registerNpcTriggerPersistence` installs schema `society.npc_trigger` v1; `installNpcTriggers` adds `society.npc_reactions` after SOCIETY propagation. Caps 16,384 triggers and validates topic/action/mode input.
2. `WorldProcessesSuite.h`, commit `04f3e95f99e483b9939f6e407a1c90ecff4ee2ef`: correct update ordering and persistence opt-in whenever SOCIETY is enabled.
3. `examples/world/world_processes_native/SocietyNpcHost.h`, commit `85dec2184d1916f8010d19cce2cbb38b745c3a52`: `SocietyNpcHost::reconcile(World&,tick)` reads authoritative transitions and invokes a native project-provided `Dispatch(NpcKnowledgeAction)` callback for existing AI behavior-tree, dialogue, quest or scripted event owners. The host derives event identity from (generation-safe trigger Entity, transition number); transient acknowledgements avoid duplicate dispatch across repeated presentation frames. On rollback, `resetAfterRollback()` discards the acknowledgement cache so the gameplay owner may reconcile the restored authoritative state.
4. `WorldProcessesExtendedLinks.h`, commit `d5014993bf0baf0b0ad0cbdf5659a5fe552fc41a`: added stable-map-ID `SocietyNpcTrigger` authoring schema with person (must point to real SocietyPerson), 16-digit hexadecimal nonzero topics, positive actionId, condition mode, once, enabled. The native map resolver creates real NpcTrigger ECS components and refuses invalid IDs and configurations.
5. `WorldProcessesMapHydration.h`, commit `fb798dd8c7c09cbf5c0868bec4cbb9e00809799a`: 16 linked categories including SOCIETY trigger, with per-type budget and stable Entity preflight.
6. `apps/studio-native/StudioSession.cpp`, commit `eef0dd01b4da8d28d7468cf71a3ea183ce54af4a`: regular Add Component, typed fields, stable person picker, Undo/Redo/Save and link-aware component deletion for the new type.
7. `WorldProcessesExtendedInspector.h`, commit `d47b9fe232b3f2b8a42c66c52c7c1511a6c06322`: read-only live SOCIETY NpcTrigger state/transition display, no live state writes.

## Boundaries

The project owns the actual AI behavior/quest/dialogue executor. The SOCIETY NPC host exposes an idempotency key and action ID but intentionally does not invent a new quest graph or duplicate an NPC blackboard. One-shot triggers are kept fired in ECS; repeating triggers fire again when their knowledge predicate becomes false and later true. Direct scripted changes to a Person's knowledge follow the same logic.
A runtime game host must register a real action callback and guarantee idempotence for authoritative game effects; replaying callbacks after rollback is a host decision. Presentation outputs should not drive authoritative changes unless submitted as validated game commands.

## Validation and next step

**Tests deliberately deferred.** No compile, CTest, live NPC gameplay, Mac Studio capture, sanitizer or save/rollback acceptance was run for Stage24. Source commits were accepted by GitHub only. Stage05 past green CI cannot validate current code.
Next: integrate Stage22 CIRCUIT switch/device, Stage23 EVOLVE ecology, Stage24 SOCIETY triggers into a concrete project demonstration with sample map/rules/native hooks, plus optional Studio gizmos. When user authorizes, run all builds/CTest/peer parity/ASan/Metal and complete merged-tree Mac quick suite before PR becomes mergeable.
