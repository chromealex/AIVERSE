---
name: aiverse-animation-ecs
description: Shared C++ presentation ECS animation components and graph player usable by any game genre.
---
# MotionWeave — ECS Animation (legacy skill path)

Subsystem name: **AIVERSE MotionWeave**. New agents should prefer
`skills/gameplay/aiverse-motionweave/SKILL.md`, public header
`<aiverselib/motionweave/MotionWeave.h>` and
`AIVERSE::MotionWeave`. The old API, skill and target remain usable.

CMake target `AIVERSE::animation_ecs`; public header
`<aiverselib/animation/EcsAnimationSystems.h>`.

Use a SEPARATE `aiverse::World` for animation presentation. Never put these
floating-point components, graph players, or asset pointers in the authoritative
`Simulation::world()`. They are excluded from checksums and rollback.

Create an entity in presentation World; add AnimationAssetComponent (model
pointer and shared compiled graph), AnimationInputsComponent,
AnimationPlayerComponent and AnimationPoseComponent. Optionally add
AnimationBudgetComponent and AnimationDiagnosticsComponent. Project gameplay
state into AnimationInputsComponent every frame, then call
`stepAnimationPresentation(world, frameDt)`. Render
AnimationPoseComponent::local/matrices if valid.

This host does not yet replace existing RtsView or automatically attach to
FPS characters. Do not snapshot presentation World: AnimationPlayerComponent
owns a shared mutable graph instance. See docs/agent-handoffs/animation-universal.md.

## Optional animation policies (all presentation-only)

- `AnimationPlayback.h`: `clipNotifies` consumes unwrapped clip time and
  returns ordered cosmetic events; `extractClipRootDelta` computes rigid
  motion requests without changing the authoritative actor.
- `AnimationMontage.h`: sectioned montage, slots, bone subtree masks;
  add `AnimationMontageSlotsComponent` to overlay it after the graph.
- `GenericRetarget.h`: bind-relative local transform retargeting using
  bone full paths plus explicit aliases. NOT Unity Humanoid retargeting.
- `AnimationMotionWarp.h`: non-looping, model-local root correction toward
  an explicit goal; no collision or character motor authorization.
- `PoseInertialization.h`: `PoseInertializer` for velocity-aware decaying
  bone offsets on a graph output; caller triggers its begin/update/apply.
- `PhysicalPoseBlend.h`: `blendPhysicalPose` with optional per-bone mask;
  `AnimationPhysicalPoseComponent` can supply an external physics pose
  to the presentation system. NOT a ragdoll solver.
- `AnimationBudgetPolicy.h`: `AnimationSignificanceComponent` and
  `assignAnimationBudgets` for stable camera-priority pose throttling.
  Does NOT schedule parallel workers or perform GPU crowd animation.

### Validation gate

Configure/build the exact target and run the standalone CTest cases:
`animation_ecs_presentation`, `animation_playback`,
`animation_generic_retarget`, `animation_montage`,
`animation_motion_warp`, `animation_inertialization`,
`animation_physical_pose` and `animation_budget_policy`.
Then run existing `animation_graphs`, `unity_anim_controller`, `fps`
and RTS gameplay/render suites. Never claim these tests pass without execution.

See `docs/agent-handoffs/animation-universal.md` for commits and blockers.

## Extended API and authoring (November 2026)

- `HumanoidRetarget.h`: `HumanoidAvatar` binds semantic body roles to
  full bone paths. `HumanoidRetargeter` transfers bind-relative global
  rotations and pelvis/root motion to the destination rig. Optional
  TwoBoneIK plants feet. Use `HumanoidProfile.h` for versioned
  `aiverse-humanoid-v1` JSON and conservative initial mapping.
- `RagdollAsset.h` generates Box3D body/hinge scene JSON with inverse
  binding offsets; `PhysicsRagdollBridge.h` samples body poses when
  `AIVERSE_ENABLE_PHYSICS=ON`. Negative `collisionGroup` suppresses
  self-contact: allocate a distinct group for each character. Recovery
  uses `AnimationRecoveryComponent` and `PoseInertializer`.
- `EcsRootMotionSystems.h` provides **fixed-point authoritative**
  `RootMotionIntentComponent`, `RootMotionStateComponent`, optional
  `RootMotionResultComponent`, `RootMotionTracksResource` and
  `addRootMotionMotorSystem` with a collision-motor callback.
  Quantize clips OFFLINE using `RootMotionBaker.h` or
  `aiverse_root_motion_bake MODEL CLIP ROOT --id N --rate N --out FILE`.
  Register persistent components/resources from the owning game module.
  Do not add this system in parallel with another transform-moving motor.
- `AnimationJobScheduler.h` supports worker-safe parallel evaluation;
  graphs with native/Lua traits or contact environment fall back to serial.
  `AnimationHotReload.h` provides transactional compile/poll/apply.
- `AnimationCrowdSystems.h` outputs GPU `BakedSample` for uncomplicated
  Idle/Walk/Run/Death clips; `fps.visual.bakedCrowd=true` enables the
  optional FPS path. Complex rigs stay on palette skinning.
- `AnimationEventDispatch.h` collects Graph Clip and Montage cues
  into a bounded cosmetic event stream; gameplay hit/damage/reload remains
  authoritative. Graph Clip nodes support
  `notifies:[{"time":0.35,"name":"step"}]`.
- Editor window `animation_assets` / operation `animation_assets_open`
  provides Humanoid Avatar, Ragdoll, Montage and Root Motion panels.
  Workspace/MCP operations: `animation_avatar_suggest`,
  `animation_avatar_validate`, `animation_ragdoll_generate`,
  `animation_root_motion_generate`, `animation_montage_validate`,
  `animation_asset_get`, `animation_asset_save`.

**Verification is deferred by user instruction.** None of these new tests
has been executed in this change set. CI/renderer, CTest, physics and
network replay parity must be done before merging or enabling defaults.
See all stage checkpoints in
`docs/agent-handoffs/animation-universal.md`.
