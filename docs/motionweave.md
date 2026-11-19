# AIVERSE MotionWeave

**MotionWeave** is the canonical name for AIVERSE's modular, genre-independent
animation subsystem: AnimationGraph, clips, blend trees, IK, Motion Matching,
Montages, root motion, ragdoll poses, inertialization and animation LOD/GPU crowds.

## Public API

Use the new CMake target and the branded public header in native projects:

```cmake
target_link_libraries(my_game PRIVATE AIVERSE::MotionWeave)
```

```cpp
#include <aiverselib/motionweave/MotionWeave.h>

// Separate client presentation ECS World; never Simulation::world().
aiverse::World presentation;
// Configure animation asset, player and output pose components first.
aiverse::motionweave::MotionWeave::update(presentation, frameDt);
```

This is the *same* implementation as the existing library, not a second engine:

| Recommended new symbol | Compatible existing symbol |
| --- | --- |
| `AIVERSE::MotionWeave` | `AIVERSE::animation_ecs` |
| `AIVERSE::motionweave` | `AIVERSE::animation_ecs` |
| `aiverse::motionweave` | `aiverse::lib::animation` |
| `MotionWeave::update(world,dt)` | `stepAnimationPresentation(world,dt)` |
| `<aiverselib/motionweave/MotionWeave.h>` | `<aiverselib/animation/EcsAnimationSystems.h>` |

All specialist libraries still have their historical
`<aiverselib/animation/...>` paths. Existing `render::AnimationGraph`,
`render::Animator` and `Pose` names are deliberately unchanged.

## Runtime design

**Presentation:** floats, AnimationGraph instances, IK and skinning reside in
a dedicated visual ECS World. FPS opts into `fps.visual.model`, configurable
Graph or baked crowd animation, preserving cube fallback. Existing RTS view
continues using its original animation path until migrated deliberately.

**Gameplay:** fixed-point, authored root motion enters the authoritative
simulation only through a collision-aware motor. Cosmetic AnimationGraph
and Montage notifies cannot trigger authoritative damage, reloads or hits.

**Authoring:** visual editor windows, Humanoid Avatar profiles, Ragdoll
descriptors, Montage and root-motion baking operations share Workspace/MCP.
Persisted asset schemas and editor IDs do not change with branding.

## Status and verification

New standalone test `tests/animation/motionweave_api.cpp` is registered as
`motionweave_api` (target `aiverse_motionweave_api_tests`).
**NOT RUN**: the user requested to defer tests, CMake builds and visual QA.
Naming changes do not imply the large animation PR is feature-complete.

Before further implementation read `AGENTS.md` and the latest entry in
`docs/agent-handoffs/animation-universal.md`. Every implementation stage
must be followed by its **own committed, append-only handoff**.

## Multithreaded presentation runtime

For a game with many animated characters, create a **persistent host-owned**
scheduler once and pass it to the three-argument MotionWeave update:

```cpp
#include <aiverselib/motionweave/MotionWeave.h>

aiverse::motionweave::MotionWeaveScheduler scheduler;
auto stats = aiverse::motionweave::MotionWeave::update(
    presentationWorld, frameDt, scheduler);
```

The existing two-argument `MotionWeave::update(world,dt)` is still
**sequential** for compatibility. The scheduler owns a persistent, bounded
work-stealing thread pool (up to seven background threads plus the frame
thread). Each frame it classifies models/compiled graphs, separates
independent clip, blend, controller, motion-matching and compatible IK
work into cost-weighted chunks, then executes them in parallel.
Native/Lua callbacks, animation traits, environment contacts, and
callback-sensitive rig operations **always run on the frame thread
after the workers have finished**. A `mainThread=true`
`AnimationWorkHintComponent` forces this lane explicitly.

`MotionWeaveScheduleOptions` exposes minimum entity count, minimum
estimated work and chunk sizing. `AnimationWorkHintComponent` also
supports a per-character cost multiplier. Graph safety/cost plans are
memoized per frame, while dense work-list allocation capacity is reused.
Invisible characters with an already valid pose carry reduced estimated
work. Caller code must not change World structure or snapshot it until
`MotionWeave::update` returns.

`MotionWeaveScheduleStats` provides eligible/parallel/serial actor counts,
batches, worker count, estimated work, unique graph count, offscreen actor
count and safe/serial phase times. These measurements are non-authoritative
presentation diagnostics; they never affect gameplay lockstep/checksums.

### Automatic backend for FPS

FPS supports `fps.visual.animationMode: "auto" | "cpu" | "gpu"`
and legacy `bakedCrowd: true`. With an eligible simple imported,
skinned model, auto mode enters GPU baked instancing at 24 visible
remote characters and exits at 12, avoiding frequent toggling.
Complex custom graphs, IK/controller requirements and `graphFile`
stay on CPU; the FPS client lazily constructs the persistent scheduler
when 16+ palette characters are present.
`fps.visual.animationStats: true` enables optional HUD diagnostics.

GPU scene draws and uploads remain on the graphics thread.
The fast crowd path reuses AIVERSE's existing baked skinning and
Renderer3D instancing; MotionWeave does not claim a new GPU compute
skinning backend.

**Test status:** all new worker/scheduler/backend CTest fixtures are
committed but **NOT RUN** (user-requested deferral). Verify compilation,
TSAN, macOS FPS screenshots, FPS save/network parity and real FPS/RTS
benchmarks before declaring production readiness.

### CPU performance fixture (deferred)

Build the standalone (no GPU/model files) benchmark after configuring
`AIVERSE_BUILD_TOOLS=ON`:

```sh
cmake --build build/ecs-test --target aiverse_motionweave_bench
./build/ecs-test/aiverse_motionweave_bench --actors 1000 --frames 120
./build/ecs-test/aiverse_motionweave_bench --actors 5000 --frames 120 --ik
```

It creates identical serial and persistent-worker Worlds, warms both,
measures average CPU frame times and verifies one output pose for parity.
The benchmark intentionally is **not registered in CTest** because timings
are machine-dependent. It must be run together with ThreadSanitizer,
real-model Metal screenshots and a representative RTS/FPS frame trace
before changing production worker thresholds. **NOT RUN**.

## Strict genre-independent contract

MotionWeave must NEVER depend on or inspect FPS/RTS/RPG modules.
Dependency direction is one-way:

```text
Any game module / game simulation / editor
      ↓ generic animation model and named parameters
MotionWeave reusable presentation ECS
      ↓ generic Pose / Notifies / Root delta
Host rendering, audio/VFX and host-owned gameplay motor
```

The core doesn't know what "fire", "attack", "move", "cast",
"reload" or another action means. Authors select parameter/event
names in their own graphs; clients project their state into the
same reusable API.

Generic `<aiverselib/animation/MotionWeaveSourceBridge.h>` exposes
`AnimationSourceFrame {sourceId,generation,model,graph,inputs}` and
`MotionWeaveSourceBridge::sync(presentationWorld, sources)`.
A bridge maintains visual ECS entity identity, resets visual players
on source replacement, copies graph-defined scalar/vector parameters
and removes missing sources. It neither evaluates animation nor
commits gameplay state. Call the normal MotionWeave scheduler
after all input projection and optional view budget configuration.

The original FPS event adapter has been moved out of the animation
library to `modules/fps/view/FpsAnimationEvents.h`.
The shared `<aiverselib/animation/AnimationActionEdges.h>` now
processes only opaque named counters/boolean state transitions, with no
FPS/RTS/action-type semantics. `playAnimationSlot` lives beside the
generic ECS `AnimationMontageSlotsComponent` definition.

## Engine View migration

The canonical scene/view type is `aiverse::View` (also
`aiverse::view::View`) in `engine/view/View.h`, linked through
`AIVERSE::view`. Host code supplies `aiverse::view::Object`
records containing stable IDs, generic `render::ModelDraw` packets,
optional AnimationGraph and named parameters. The generic View
manages independent presentation ECS states, animation LOD budgets,
worker scheduling and pose-based scene submission.

Use `View::update(span,dt)` before `View::submit(renderer,span)`
or `View::submitOne(renderer,object)`. A packet may contain a model
with no animation graph. View does not depend on RTS, FPS, camera
controller, game rules, network, combat, map or terrain modules.

A transition adapter still exists in
`modules/rts/view/RtsView.cpp` and `RtsViewDraw.cpp`: it projects
only simple, eligible compiled animation graphs into the generic
View and delegates their **final ModelDraw submission** to
`View::submitOne`. Complex RTS procedural animation, contact IK,
genre-specific attachments/FX/fog/terrain remain in the game
host until each has a generic render contract. The compatibility
class `game::RtsView` remains for older game code; it is not the
canonical engine View and should not be used in new projects.

### Host migration status

Both existing game frontends now send at least their supported
animated-model draws through the universal engine
`aiverse::View::submitOne`:

- The legacy game-side `game::RtsView` delegates a strictly
  eligible subset of pure AnimationGraph pose evaluation and the
  final styled model draw.
- The FPS reference client projects its existing CPU Graph / GPU
  Baked `render::ModelDraw` through `View::submitOne`, preserving
  the precomputed pose. Its replicated gameplay state never enters
  engine View.

The common CMake name `AIVERSE::View` (legacy lowercase
`AIVERSE::view` also works) and public
`<engine/view/View.h>` describe the preferred engine API. The
old `aiverse_game_view`/RTS-specific presentation remains
compatibility code during migration; it is **not** identical to
the new generic View yet. Shared camera/culling/terrain/lighting
composition and full legacy game adapter removal require further
refactoring. No source-level genre dependency is allowed inside
`engine/view`.

