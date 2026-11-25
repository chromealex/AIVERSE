# AIVERSE Building Generator — Stage 20 fixed-tick ECS actor retirement

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Stage goal and architecture
Stage 19 introduced a fail-closed per-actor retirement guard. Stage 20
introduces a deterministic world-owner queue to invoke it after the real
circulation tick and defer destruction of ECS riders until the lift has
reached a physical open-door landing. The queue is not a second entity
system: actor identity is the existing Entity packed index+generation,
physical state belongs to existing SpatialWorld, lift and door state to
BuildingCirculationEcsScene and movement to ECS Body.

## Modified files / code
- NEW engine/buildings/BuildingActorLifecycleTick.h:
  BuildingActorRetirementReason (Destroyed, Disconnected, SceneTransfer),
  BuildingActorRetirement, BuildingActorLifecycleTick,
  BuildingActorFixedTickResult<Body>,
  BuildingActorLifecycleTickOwner<Body>.
  requestRetirement(ecs,scene,actor,reason,error) validates actor, scene
  identity and existing passenger ownership, then idempotently enqueues.
  advanceFixedTick(ecs,scene,spatial,navmesh,elapsedMs) runs real
  scene.advance first and then processes retirement only on complete
  collision + ECS movement publish. afterCommittedTick exists for manual
  engine integration (caller MUST check prior scene tick succeeded).
  In-flight riders are not destroyed: prepareActorRetirement only succeeds
  with existing physical landing doors open and authoritative host/marker
  ownership. An accepted actor enters ECS destroyLater; game ECS flush()
  remains the single structural barrier. Stale scene generations are
  discarded; illegal externally killed entities produce an explicit error.
  Pending entities remain alive; resetAfterWorldDestruction is explicit.
- engine/buildings/BuildingRuntime.h exports the new adapter.
- skills/world/aiverse-building-style/SKILL.md Stage 20 documents exact
  owner-phase order; existing skill catalog entry is reused.

## How game owner should integrate
1. Construct one BuildingCirculationEcsScene<Body> and one
   BuildingActorLifecycleTickOwner<Body> per scene (never also use
   BuildingLiftGameSession for the same lift).
2. Existing network/actor owner must immediately revoke disconnected or
   despawning actors' ability to send commands, and call
   requestRetirement(ecs,scene,actor,Disconnected/Destroyed/SceneTransfer).
3. On fixed owner tick invoke lifecycle.advanceFixedTick(...). This first
   publishes world collision and ECS displacement, then attempts retirement.
   Only a physically open landing allows removing an attached lift rider.
4. At the established ECS barrier, AFTER all ECS iteration,
   ecs.flush() performs queued entity destruction. Do not flush inside a
   component iteration or immediately inside a network packet handler.
5. Use scene.detachIfVacant for ordinary streaming; do not clear a
   queue/force-unload an occupied scene. For global world shutdown,
   reconcile riders outside this adapter before forced teardown.

## Boundaries, risks, tests
This stage is an engine adapter, not a wired example game runtime.
The actual game's authoritative character/controller despawn path still
must call requestRetirement, and the game's fixed-tick loop must call
advanceFixedTick; no claim that a live game already uses it.
Session re-authentication suppression, movement/PHYSIS motor attachment,
reconciliation on building rollback, cross-scene transfers, and renderer
fences remain owned by the corresponding game systems.

Tests are deliberately deferred at user request. No C++ compiler, CTest,
Mac Metal/GL33 screenshots, Studio TypeScript or multiplayer runtime test
was run in Stage 20. Source commits and readback are not proof of compile.

## Exact provenance
- 1e837b521ce981d6bc5935fd1c6cc5d96713620c — initial queue.
- 59548079daec0e5dd88991af90af0b993541888a — runtime umbrella.
- 362df18dd347e5bce043db7896d6221c77621ae7 — fixed-tick ordering.
- 936b085404343dc9c4f0d04506d465c9b94b76bb — skill.
- This handoff publication itself receives the next commit.

## Next concrete step
Bind the actual game entity destroy/disconnect hooks and authoritative
fixed-tick scheduler to this owner; add real movement collision sweeps
for stairs and lift arrival/exit, and nav::Source derived from generated
building solids. When user requests testing, compile this header in
native game targets and exercise idle/occupied/faulted/unloaded/reconnect
cases, plus actor generation reuse and server/client packet replay.
Reconcile the significantly diverged main branch before merging PR 114.
