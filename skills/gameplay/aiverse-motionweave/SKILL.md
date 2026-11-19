---
name: aiverse-motionweave
description: Modular ECS animation system for AIVERSE native projects across FPS, RTS, RPG and cinematics.
---
# AIVERSE MotionWeave

Canonical public API:
- Header: `<aiverselib/motionweave/MotionWeave.h>`
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

For FPS prefer `fps.visual.animationMode="auto"`: simple skinned crowds can
use GPU baked instancing (24 enter/12 exit by default); custom graphs and rigs
use the CPU scheduler. Optional `fps.visual.animationStats=true` shows a
non-authoritative debugging overlay. The independent CTest targets are
`motionweave_workers`, `motionweave_hybrid`,
`motionweave_cost_balancing`, `motionweave_backend_selection`, and
`motionweave_graph_cost_cache`: **NOT RUN** pending user's test stage.

## Non-negotiable genre-independent architecture

NEVER introduce genre/app includes into `engine/render`,
`libraries/include/aiverselib/animation` or
`libraries/include/aiverselib/motionweave`.
A generic source frame carries stable source ID, generation,
model, graph and named animation parameter values; the source adapter
`MotionWeaveSourceBridge` projects those into a presentation ECS World.

The game module owns translation of its movement/combat/worker/build
or any other state into graph parameters and cosmetic event requests.
The engine core provides no named gameplay actions and does not
contain conditional FPS/RTS paths. Reuse one animation system,
one set of components and one set of algorithms for every genre.

Core action edges use only `<aiverselib/animation/AnimationActionEdges.h>`:
a revision/generation and arbitrary named counters and flags. FPS-only
`shots/reloadUntil/health` conversion lives under
`modules/fps/view/FpsAnimationEvents.h`, not in the MotionWeave library.
Use a different host adapter for every other game's own data layout,
without duplicating generic event/animation algorithms.

