---
name: aiverse-motionweave
description: Modular ECS animation system for AIVERSE native projects across FPS, RTS, RPG and cinematics.
---
# AIVERSE MotionWeave

Canonical public API:
- Header: `"engine/animation/MotionWeave.h"`
- CMake: `AIVERSE::MotionWeave`
- C++ namespace: `aiverse::motionweave`
- Scheduler: `motionweave::MotionWeave::update(presentationWorld, dt)`

The animation World is **presentation-only**. NEVER add float graph state,
pose caches, model pointers, visual IK or cosmetic notifies to the
authoritative `Simulation::world()`. Deterministic root motion uses the
fixed-point motor adapter, not sampled presentation floats.

Existing `AnimationGraph`, `Animator`, `Pose` and special-purpose headers under
`aiverselib/animation/` are part of MotionWeave. Older consumers may
continue using `aiverse::lib::animation` and `AIVERSE::animation_ecs`.
Do not fork/duplicate the runtime for another genre.

Library catalog id: `motionweave` (`python3 tools/libraries/catalog.py info motionweave`); capability area
A3 in `docs/capability-status.json`. Exists: montages with sections, slots and bone masks, clip notifies, root
motion extraction/baking, motion warping, humanoid and generic retargeting, pose inertialization, crowd
budgets, facial rig, Box3D ragdoll bridge. Not done (2026-11-22): audio-driven lip-sync (Vivid visemes are set
by hand), live game-scene/prefab binding, HTML5 Studio viewport.

Read `docs/motionweave.md`, `docs/animation-graphs.md`,
`docs/agent-handoffs/animation-universal.md` and `AGENTS.md`.
**After each code stage**, commit a separate dated handoff entry listing
SHA, changed files, API contracts, actual tests/NOT RUN, risks and next task.

## Multithreading and backend selection

To enable persistent multithreading in a native host:

```cpp
aiverse::motionweave::MotionWeaveScheduler scheduler;
auto stats=aiverse::motionweave::MotionWeave::update(world,dt,scheduler);
```

Keep the scheduler alive for the game view's lifetime. The older two-argument
`MotionWeave::update` remains sequential. Call `update` only after projecting
input parameters into a **presentation-only** ECS World. Never mutate, remove
components or snapshot the World while workers execute.

The scheduler computes safe clip/blend/IK/MotionMatching entities on cost-balanced
worker jobs. Lua/native callbacks, traits and environment contacts execute
on the caller thread after a complete barrier. Force serialization with
`AnimationWorkHintComponent::mainThread=true` when a host has another
thread-bound integration; use `multiplier` to guide work partitioning.

For FPS prefer `actor.visual.animationMode="auto"`: simple skinned crowds can
use GPU baked instancing (24 enter/12 exit by default); custom graphs and rigs
use the CPU scheduler. Optional `actor.visual.animationStats=true` shows a
non-authoritative debugging overlay. The independent CTest targets are
`motionweave_workers`, `motionweave_hybrid`,
`motionweave_cost_balancing`, `motionweave_backend_selection`, and
`motionweave_graph_cost_cache`: **NOT RUN** pending user's test stage.

## Non-negotiable genre-independent architecture

NEVER introduce genre/app includes into `engine/render`,
`libraries/include/aiverselib/animation` or
`engine/animation`.
A generic source frame carries stable source ID, generation,
model, graph and named animation parameter values; the source adapter
`MotionWeaveSourceBridge` projects those into a presentation ECS World.

The game module owns translation of its movement/combat/worker/build
or any other state into graph parameters and cosmetic event requests.
The engine core provides no named gameplay actions and does not
contain conditional FPS/RTS paths. Reuse one animation system,
one set of components and one set of algorithms for every genre.

Core action edges use only `"engine/animation/AnimationActionEdges.h"`:
a revision/generation and arbitrary named counters and flags. FPS-only
`shots/reloadUntil/health` conversion lives under
`engine/view/ActorAnimationEvents.h`, not in the MotionWeave library.
Use a different host adapter for every other game's own data layout,
without duplicating generic event/animation algorithms.

## Stage 180: semantic facial rig and existing IK (source-only)

Use `"engine/animation/FacialControlRig.h"` and
`FacialControlRig::compile(model, authoredV1, declaredParameters)`.
This compiles explicit importer-authored **mesh-local morph names** and
optional existing native `render::AnimationRig` `rig` operators
(`aim`, `angularLimit`, `twoBoneIK`, `fullBodyIK`,
`transform`). Never infer a morph channel from its index on a different
mesh. Bind per-actor `FacialControlState`; call `apply()` AFTER the
single Graph/Animator pose evaluation and BEFORE final matrices.
Failure leaves source pose and facial state unchanged. On asset reimport,
recompile and reset state; the runtime rejects stale node/bind/morph
signatures. See `docs/motionweave-facial-control-rig.md`.

For ECS projects add the optional
`AnimationFacialControlComponent{rig,controls,state,weight,enabled}`
to the **presentation World**. `MotionWeave::update()` integrates it
after Graph/Montage/physics/recovery; controls need not be declared by
the parent Graph. The scheduler allows worker-safe **morph-only** rigs;
any facial rig with native IK or a host environment runs on the **owner
thread** alongside other callback-sensitive Graphs. No duplicate
Animator, gameplay authority or network checksums.

Deferred executable/CTest:
`aiverse_animation_facial_control_rig_tests` /
`animation_facial_control_rig`. **NOT RUN**; no native compile/Metal/
Vulkan/GLES, QA or crowd benchmark pass was claimed.


## Stage 183: reusable facial preview session for native hosts (source-only)

For an artist-generated `facial-rigs/*.json` document from the HTML5 Studio
branch, use `<aiverselib/animation/FacialPreviewSession.h>` and
`aiverse::lib::animation::FacialPreviewSession`. Compile a new session
transactionally from the real imported `render::Model`, JSON v1 and typed
named float/vector overrides. Use `scalar()/vector()` for live expression
changes; `apply(model,alreadyEvaluatedPose,dt,error)` goes AFTER one
Animator/Graph evaluation and BEFORE `worldMatrices`. The per-instance
session owns its own smoothing; it does NOT own the Model or GPU lifetime.
Do not reimplement expressions, IK or Graph evaluation in Studio.

The native ModelView host demonstrates `--facial-rig project/facial-rigs/name.json`,
`--face jawOpen=.8` and `--face-vector lookAt=2,1,3` with read-only saved
JSON hot reload; unknown Model nodes/channels reject. The native
`aiverse_modelview` window is NOT the HTML5 Studio embedded GPU viewport.
Studio still needs a separate typed IPC/real asset-selection integration
after branches can be safely combined. Docs:
`docs/motionweave-facial-native-preview.md`, Stage 183 handoff. The
extended `animation_facial_control_rig` source fixture is **NOT RUN**.
