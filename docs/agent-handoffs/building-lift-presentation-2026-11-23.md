
# AIVERSE Stage 17 handoff: lift GPU presentation bridge
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Pull request: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Why this stage

Stage 13 built stairs and shafts in StructureGraph.
Stages 14-15 added native nav::Mesh routing, SpatialWorld dynamic platform
collision, action-level ECS rider navigation and actor carry.
Another parallel agent added Stage 16 ECS, landing door obstruction and
authorized lift snapshot/command protocols. This stage fills the missing
Renderer3D presentation boundary without creating an alternate scene engine.

## Actual code changes

- engine/buildings/BuildingLiftPresentation.h (new):
  BuildingLiftVisualAssets: one render::Model per dynamic platform and
  one half-width reusable door model per lift feature, allocated once.
  The door model has runtimeMutable node, dedicated geometry, autoLod off,
  and does not merge with static scenery.
  liftDoorVisualOpen: derives door fraction from authoritative Open /
  Closing / Traveling / Opening / Offline phases.
  buildingLiftVisualFrame: reads authoritative host controllers and
  physical door landing metadata to return door leaf world positions,
  platform world poses and state revisions. Only current-floor doors
  animate; target-floor doors stay closed until arrival. Does not
  allocate a GPU resource or modify physics.
- engine/buildings/BuildingLiftGameSession.h:
  makeVisualAssets(style) to build reusable CPU resources at spawn.
  visualFrame() returns invalid when the game scene is unattached or
  faulted, so uncommitted cabin/rider positions are not rendered.
- engine/buildings/BuildingRuntime.h:
  includes the presentation adapter in existing game-client facade.
- docs/building-functional-recipes.md, examples/world/runtime-building/README.md,
  skills/world/aiverse-building-style/SKILL.md: ownership, callbacks,
  no-remesh, scene fault and GPU lifetime guidance.
- docs/agent-handoffs/building-lift-presentation-2026-11-23.md:
  this continuation handoff.

## Existing system contracts verified by source inspection

- Existing render::Model contains runtimeMutable Node and a batching
  GeometryPoolPolicy::Dedicated switch to prevent static consolidation.
- Existing runtimeLiftPlatformModel() builds a reusable CPU platform model.
- Existing BuildingLiftGameSession::advance() gates snapshots on successful
  collision and ECS rider publication.
- Existing BuildingLiftDoorOwner::poses() yields physical landing geometry.
- Authoritative door status, passenger IDs and network frame remain
  owned by existing Stage-16 code from the parallel agent.

## Verification and limitations

Per user's "тесты потом" request, no compiler, CTest, web Studio build,
Metal screenshot or full gameplay render test has been performed for
this stage. Source writes and current branch readback only.
No claim of rendering into a real game viewport, physical articulated
door animation or multiplayer demonstration. GUI renderer integration
must explicitly upload the reusable CPU models through its GPU owner,
create the model instances and apply the emitted transforms.

Physical landing-door colliders currently switch at authoritative phase
boundaries; presentation interpolates a visual sliding fraction. Thus
the visual leaf may be in transition while PHYSIS considers the doorway
closed/open based on its actual state. Keep collision/boarding permission
bound to the server state. To synchronise animated physical leaf collision
with render interpolation, enhance the door physics owner in a later
stage under existing world-owner phase.

## Next handoff tasks

1. Integrate this adapter into the actual Renderer3D scene-instance owner
   and native Studio ModelView with persistent GPU resource leases.
2. Render physically segmented doors with matching PHYSIS collision motion,
   obstruction re-open safety and character boarding volumes.
3. Add authoritative actor attachment/visual interpolation across network
   snapshots; keep clients non-authoritative.
4. When user authorizes testing: Clang/GCC compilation (this revision is
   not compiled yet), renderer adapter typecheck, game owner smoke with
   real SpatialWorld/navmesh, Studio TS and Mac Metal capture.
5. Reconcile stacked parent branch, parallel changes and remaining legacy
   19/20 sized factory bug before main merge.
