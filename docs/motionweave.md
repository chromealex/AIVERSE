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
#include "engine/animation/MotionWeave.h"

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
| `"engine/animation/MotionWeave.h"` | `"engine/animation/EcsAnimationSystems.h"` |

All specialist libraries still have their historical
`<aiverselib/animation/...>` paths. Existing `render::AnimationGraph`,
`render::Animator` and `Pose` names are deliberately unchanged.

## Runtime design

**Presentation:** floats, AnimationGraph instances, IK and skinning reside in
a dedicated visual ECS World. FPS opts into `actor.visual.model`, configurable
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
#include "engine/animation/MotionWeave.h"

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

FPS supports `actor.visual.animationMode: "auto" | "cpu" | "gpu"`
and legacy `bakedCrowd: true`. With an eligible simple imported,
skinned model, auto mode enters GPU baked instancing at 24 visible
remote characters and exits at 12, avoiding frequent toggling.
Complex custom graphs, IK/controller requirements and `graphFile`
stay on CPU; the FPS client lazily constructs the persistent scheduler
when 16+ palette characters are present.
`actor.visual.animationStats: true` enables optional HUD diagnostics.

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

Generic `"engine/animation/MotionWeaveSourceBridge.h"` exposes
`AnimationSourceFrame {sourceId,generation,model,graph,inputs}` and
`MotionWeaveSourceBridge::sync(presentationWorld, sources)`.
A bridge maintains visual ECS entity identity, resets visual players
on source replacement, copies graph-defined scalar/vector parameters
and removes missing sources. It neither evaluates animation nor
commits gameplay state. Call the normal MotionWeave scheduler
after all input projection and optional view budget configuration.

The original FPS event adapter has been moved out of the animation
library to `engine/view/ActorAnimationEvents.h`.
The shared `"engine/animation/AnimationActionEdges.h"` now
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
`engine/view/game/GameView.cpp` and `GameViewDraw.cpp`: it projects
only simple, eligible compiled animation graphs into the generic
View and delegates their **final ModelDraw submission** to
`View::submitOne`. Complex RTS procedural animation, contact IK,
genre-specific attachments/FX/fog/terrain remain in the game
host until each has a generic render contract. The compatibility
class `game::GameView` remains for older game code; it is not the
canonical engine View and should not be used in new projects.

### Host migration status

Both existing game frontends now send at least their supported
animated-model draws through the universal engine
`aiverse::View::submitOne`:

- The legacy game-side `game::GameView` delegates a strictly
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

### Render-resource owner (Stage 99)

MotionWeave graph and pose state does **not** own GPU Model allocations.
Generic native hosts can use `aiverse::render::ModelResourceCache`
(`engine/render/ModelResourceCache.h`) with a separate presentation
`World`/`aiverse::View`. The cache owns the model map and shared
texture leases. On model/graph replacement, hosts must detach affected
`AnimationSourceFrame` and pose/graph players **before** moving the
old model to the cache's deferred retirement queue. At the next safe
host frame boundary, `collectRetired(device)` invokes the explicit
`gfx::Device::waitForResourceIdle` barrier and releases only resources
whose previous GPU submissions are complete. Never derive completion
from `frameSerial()` or a fixed number of frames in flight.

Legacy `game::view_adapter::GameModelResourceCache` and
`GameModelPreparationService` are aliases to the universal render
classes. Material preview sphere/box resources in `game::Portrait`
are the first independently owned real editor consumer. The complete
RTS scene compatibility adapter is not yet retired. Deferred
GPU/backend and animation parity tests are **NOT RUN**.

### Nonblocking GPU model retirement (Stage 100)

Model and texture GPU lifetime is independent of MotionWeave player state.
A native host can own `render::ModelAssetProvider` (from
`engine/render/ModelAssetProvider.h`) without loading RTS/FPS headers,
then submit its borrowed model through `aiverse::View::submitModel` or
the generic ordered `ScenePacketBatch`. After graph/source invalidation,
detach the owning presentation ECS sources before moving models into
`render::ModelResourceCache` retirement. Only one animation owner
evaluates each graph; packet submission must not tick its graph a second
time.

Stage 100 adds asynchronous per-batch retirement markers via
`gfx::Device::beginResourceRetirement`,
`resourceRetirementComplete`, and `endResourceRetirement`.
`ModelResourceCache::collectRetired` polls rather than waiting for the
whole GPU on supported backends. The old synchronous wait is only a
fallback or explicit shutdown barrier. Never use a frame counter,
fixed delay or time-since-last-use as a GPU completion condition.
The new `game::Portrait::renderStaticModel` is a direct native-model
preview consumer using the canonical View, with no game-side owner
required for that code path. Dynamic RTS models still use compatibility
presentation and rendering.

**NOT RUN:** deferred native/Metal/Vulkan/GL/WebGL2 build, runtime,
GPU-lifetime, sanitizer and visual parity checks.

### Stage 101 — async model source preparation and GPU retirement pressure

Animation graphs remain separate from render-resource loading. Native
project hosts can use `render::ModelAssetProvider::requestAsync(path)`
for bounded, single-worker CPU model import, then call
`publishReady(device, maxCount)` on the owning render thread before
constructing `aiverse::View` packets. The CPU queue records source
revisions: invalidating a model source makes any older worker result
unpublishable even if the CPU task completes later. MotionWeave source
identity/generation and any live pose/graph players must still be
detached before replacing a resident Model.

`render::ModelResourceCache` now exposes model-count-based retirement
pressure telemetry and a soft admission limit for NEW generic model
loads. Existing graph sources continue to render; a saturated backlog
must never force early GPU destruction. The default is 128 retired
Model objects, NOT a byte-accurate GPU memory budget. Editor's Statistics
panel reports pending GPU model retirements and fence fallbacks.

The editor Model window provides an opt-in native-source preview for
static unskinned models, using an independently owned
`render::ModelAssetProvider` and
`game::Portrait::renderStaticModel`. Default fully authored
RTS/character portraits still use their previous presentation path,
preserving material overrides, attachments and animation playback.
The raw-model inspection path is not a parity replacement.

**NOT RUN:** CMake/compiler, CTest, Metal/Vulkan/GL/WebGL2 GPU tests,
native/editor preview, sanitizers and simulation/animation visual
regressions. Keep stages independent and do not claim the old GameView
has been removed.

### Stage 102: model source priorities and estimated GPU-retirement memory

The host owns one `render::ModelAssetProvider`, or a separate provider
per presentation scope, not one per animated actor. For native model
imports, `requestAsync(source, ModelLoadPriority)` selects
`Background`, `Visible` or `Interactive` for queued CPU jobs; an
active worker is never preempted. Call
`publishReady(device,maxCount,budgetMs)` at the render-thread frame
boundary, with a hard model-count cap and an advisory elapsed-time cap.
The first GPU upload may exceed `budgetMs`; nothing is published from
inside a worker. Preserve source revisions, and detach any old
`AnimationSourceFrame`, graph, pose and borrowed `Model*` before
invalidation. No second MotionWeave evaluation is introduced.

`ModelResourceCache` can now estimate the *known, model-owned*
retirement footprint and apply an optional byte-derived admission
limit. `ModelResidentEstimate` deliberately does not charge borrowed
texture leases or shared geometry pages and is NOT an exact VRAM
reading. Never release an object because a limit is reached; only
its own completed GPU fence authorizes destruction.

The standalone `aiverse_modelview --provider --no-merge` is a second
real generic `aiverse::View` consumer for static raw assets, alongside
the editor's opt-in independent raw preview. Precomputed poses on the
legacy animated modelview path remain authored by the existing host;
View only submits them once. The game-specific `GameView` adapter is
still in use elsewhere and has not been retired.

**NOT RUN:** all CMake, CTest, native modelview/editor, GPU backend,
sanitizer, snapshot and visual parity checks remain deferred.

### Stage 103 — multi-worker source preparation and single-authority View display

The general model resource owner can now prepare multiple independent
model files on CPU workers without involving the ECS World or GPU:
`render::ModelAssetProvider::setCpuWorkerCount(1..8)`,
`setCpuMaxQueued`, and `requestAsync(source,priority)` forward to
`AsyncModelCpuQueue`. The default remains one worker; an editor or
native host can opt into more. Workers have no knowledge of genre,
FPS/RTS player types, scene packets or MotionWeave graph evaluation.
`ModelAssetProvider::publishReady(device,...)` uploads prepared Models
on the render/device owner thread. Authoring hosts needing validation
before GPU publication may instead call `takePrepared(result)`, compile
their rig against `result.model`, then call `publishPrepared(device,
freshGenerationKey,std::move(result.model))`. This path uses the same
staging transaction and rejects publication while a Model read lease is
active. `cancelAsync(source)` invalidates pending CPU results without
evicting the last-good resident Model; `stopAsync()` joins the reader
before its project is destroyed. Retire the old generation only after
successful publication, detaching pose and renderer caches first.

Studio's native facial preview uses this split path. One `ModelReadLease`
spans pose evaluation through the synchronous Renderer3D submission.
Project changes drain the old owner and its texture cache, so identical
relative paths cannot reuse another project's textures. CPU preparation
runs in the worker; GPU upload/retirement and the single Animator stay
on the render thread. Bad imports retain last-good ownership and report
the rejected generation instead of drawing a stale asset.

Priorities are `Interactive`, `Visible`, `Background`. Pending
jobs of equal priority are FIFO; after sustained high-priority
dispatches, an oldest queued request gets scheduled to reduce
background starvation. No executing job is preempted. Each CPU
result contains its source revision; on asset invalidation it is
discarded rather than publishing a stale Model, and on shutdown all
worker futures are joined before the reader/texture cache is released.
Caller-provided `FileReader` callbacks must be reentrant when more
than one worker is configured.

Shared GPU texture ownership is measured once by
`ModelTextureCache::gpuOwnerEstimate()`, never per borrower. The
device-global geometry pool remains a separate resource owner; these
figures are source-derived RGBA8-equivalent diagnostics, not exact
driver VRAM usage.

The editor `SceneRenderable` source now uses the canonical
`aiverse::View::submitModel` and `submitMesh` for ordinary model
and mesh submissions. Selected/hovered specialized mesh silhouettes
retain their previous renderer-specific path. The owner still
constructs the ModelDraw transformation, tint and outline; View
does not run graph animations a second time. The legacy RTS
portrait/type-appearance compatibility layer remains active.

**Validation NOT RUN:** native CMake compiler, CTest, sanitizer,
actual GPU/Metal/Vulkan/GL/WebGL, FPS/RTS/editor screenshots,
multithreaded loader contention and visual parity have not been
verified. See the Stage 103 handoff for exact SHA and next action.

### Stage 104 — persistent multi-worker import and View selection styles

The native `render::AsyncModelCpuQueue` now reuses 1–8 persistent
`std::thread` workers instead of spawning a new asynchronous task
for each model file. It uses condition-variable dispatch, bounded
waiting/ready queues, source revision invalidation and the existing
Visual/Interactive/Background priorities. Configuration is host-owned:
the reader and CPU image cache must be reentrant; all GPU upload,
source publication and MotionWeave pose state remain on the render
owner thread. Changing the worker count joins in-flight tasks;
do not reconfigure during an interactive rendering frame.

Shared GPU geometry is accounted by page identity, not by the
number of animations or imported Models. The
`geometryPoolReferencedStats(device,modelPointers)` API can accept
models from multiple render host owners in ONE collection, avoiding
double charging a page referenced by many models or views.
`ModelResourceCache::geometryPoolModelReferences` includes
currently resident and GPU-retired models. The device's global
geometry pool remains the authoritative page capacity owner, and
shared textures remain counted by their texture-cache owner.

`aiverse::view::MeshObject` now has genre-independent
`selectionOutline` / `selectionColor` styling. The editor's
`SceneRenderable` selected/hovered mesh submissions and selection
edge lines are routed through `aiverse::View::submitMesh/submitLine`
with the legacy renderer-specific silhouette implementation
preserved internally. This does not tick an animation graph or add
gameplay selection components to MotionWeave.
Full `game::GameView` retirement is still unfinished.

**NOT RUN:** native build, CTest, desktop/mobile GPU tests, editor
selection visual parity, CPU thread sanitizer and geometry pool
GPU-lifetime checks. See Stage 104 agent handoff.

### Stage 105 — cross-host GPU resources and generic authored portrait packets

`render::RenderResourceOwnerGroup` is a non-owning aggregation
facade over multiple independent native `ModelResourceCache` hosts.
A group computes unique model and shared texture ownership, includes
retired models waiting on GPU fences and deduplicates shared geometry
pages by their device-global identity. Its `Ready`,
`PendingGpuRetirement` and `DeviceUnhealthy` statuses are
diagnostics/admission policy, not a license to free resources early.
Pending GPU work is expected, not an error. Estimates use known
source geometry and RGBA8-equivalent texture metadata; never call
them exact driver VRAM occupancy.

Native editor raw-model preview now groups the main scene,
portrait preview and raw-source provider, with an optional
group-wide estimated admission budget. Failed GPU health pauses
new model publication, leaving all GPU-retired models alive
until correct fence completion can be established.

`game::Portrait::renderAppearance(const render::ModelDraw&,
Camera,background)` is a generic synchronous offscreen
consumer that keeps the host-selected material override,
node-material mapping, geometry policy, team tint, variant
and any precomputed pose. Raw-model, precomputed-pose and
RTS-resolved **static** portraits delegate to it. Complex
animated portraits retain their existing RTS compatibility
fallback and its one MotionWeave graph evaluator; no second
player or render-side animation tick is introduced.

**Validation NOT RUN:** new CTest contracts, native compiler,
GPU fence/device loss recovery, renderer/portrait screenshot
parity, FPS/RTS tests and thread sanitizers are deferred by
the user. Follow Stage 105 in the animation handoff.

### Stage 106 — expiring GPU owner references and verified failure states

Multiple native model/resource owners can be observed through
`render::RenderResourceOwnerGroup`. Its registrations now carry
the caches' `weak_ptr` lifetime tokens, so a destroyed cache
is skipped rather than accessed. The group is still
device-owner-thread-only; a weak reference does not make
simultaneous GPU resource teardown and snapshot thread-safe.
The group doesn't own or shut down render resources.

GPU retirement now distinguishes a normal outstanding fence
from an actual failed completion token via
`gfx::Device::resourceRetirementStatus`. A failed Model
retirement batch remains alive, even when newer tokens signal
success. Only a successful full-device GPU idle barrier
permits cleanup and `abandonRetirementTokenAfterIdle`.
`RenderResourceGroupStats::health` reports
`RetirementFenceFailed` separately from
`PendingGpuRetirement` and `DeviceUnhealthy`.
These statuses only affect NEW resource admission, never
authoritative gameplay or existing MotionWeave graph clocks.

The model material-preview shape now delegates offscreen draw
to the same generic `Portrait::renderAppearance` +
`aiverse::View::submitModel` path as native static models
and precomputed poses. Material authoring still happens through
the old RTS-compatible host; dynamic/complex animation fallback
has not been removed and retains exactly one animation writer.

**NOT RUN:** compilation, CTest, GPU/device-loss tests,
editor screenshots, FPS/RTS parity, thread sanitizer and CI.
Stage 106 handoff lists exact source refs and next step.

### Stage 107 — scoped device owners, deterministic retirement trace and independent authored appearance packet

The generic `render::ModelResourceCache` now explicitly binds to
one `gfx::Device` and one owning thread via
`bindResourceDevice(device)`. Cross-device or cross-thread GPU
model mutation is rejected. `RenderResourceOwnerGroup` supports
`registerScoped(device,cache)` returning a move-only scoped
registration. The editor preview uses these scoped registrations
rather than leaving resource-owner observer pointers in an
unbounded registry. Resource owners do not outlive the device,
and a ModelResourceCache may be rebound only after a successful
GPU-safe shutdown. The old CPU-only `add(cache)` survives for
existing tools but is not the recommended device-bound route.

New opt-in `RetirementDiagnosticEvent` callbacks on the
resource retirement queue, and `RetirementStatusReplay`
with `script/issue/advance/status/events`, allow deterministic
synthetic GPU token timing and pending/failure/idle-recovery traces.
Synthetic results never replace a real GPU completion fence and
cannot authorize GPU resource destruction on hardware. Failed
markers stay retained until the real backend supplies a successful
full idle barrier.

`engine/view/AppearancePacket.h` now owns generic visual
packet composition for material overrides, node-material
assignment, variant, team/tint, geometry policy and optional
graph/parameter inputs. The RTS static preview adapter maps
its typed selection and authored placement into the same
`view::AppearanceSpec`, then submits the resulting canonical
`view::Object`. The RTS host still chooses type, placement,
material library and gameplay policy; no RTS/FPS header enters
the generic View code. Static/precomputed appearance uses
`submitPrecomputedAppearance` and MUST have no attached graph.
For graph appearances, the owning View must call
`update` exactly once, then `submitOne`; do not create a
second MotionWeave writer.

**Validation NOT RUN:** native C++ builds, all CTest fixtures,
metal/Vulkan/GL GPUs, editor/portrait FPS/RTS visual parity,
sanitizers and CI. New deferred fixtures and the exact next
stage are recorded in the Stage 107 handoff.

### Stage 108 — GPU device epochs, bounded diagnostics and shared material/graph authoring

Native host model caches now support
`RenderDeviceLifetime::Lease` and
`ModelResourceCache::bindResourceDevice(device,lease)` to
distinguish re-created GPUs even when a platform reuses the
same `gfx::Device*` address. `RenderResourceOwnerGroup`
supports `registerScoped(lease,cache)` and rejects expired
generations. The host must retire its device epoch only
after safe GPU model-cache shutdown. This is NOT device
loss recovery or a way to release unresolved fence-retired
resources.

`RetirementDiagnosticRing` stores a bounded, nonowning
history of resource retirement events; the editor keeps
128 events and reports dropped/overflow records without
holding any GPU resources. Diagnostic callbacks do not
change resource lifetime or GPU completion semantics.

New `engine/view/AppearanceAuthoringService.h` separates
material library inheritance and immutable animation graph
compilation from RTS policy. It accepts library JSON,
texture-loader and warning callbacks, FileReader, model,
operations/traits and optional named graphs. Both type
appearance compilation in RtsLook and runtime control
graph compilation in RtsAnimate now use this generic
service. Existing MotionWeave pose/graph players still
have one owner; this service only creates immutable
programs and does not evaluate or advance them.
Authoring changes do not affect authoritative gameplay
tick, movement, networking or serialization.

The remaining legacy GameView still chooses type/weapon,
procedural animation, materials per entity and attachment
policies. Full dynamic genre-neutral appearance state
migration and real visual parity remain open.
**NOT RUN**: native compiler, CTest, renderer/GPU validation,
FPS/RTS screenshots, sanitizer and CI.



## Source model reimport without losing compatible playback (Stage 146)

`MotionWeaveSourceBridge::forgetForReimport(World&, sourceIds)`, surfaced as `View::forgetSourcesForReimport(ids)`, is a synchronous, optional pre-retirement handshake. Call **before** old CPU Model/compiled Graph destruction; the bridge first captures named graph state and clip phases from a valid presentation player, then forgets its ECS entity. The next `sync` can consume the seed once when logical source ID/generation, exact copied rig names/parents, explicit **different nonzero** old/new publication epochs and a distinct Model address all match. No old pointer is retained/dereferenced; the old address is a value-only conservative rejection guard. The record is bounded to 8192 sources and expires after 120 source-sync calls. The existing `forget` and `clear` APIs intentionally discard continuity, suitable for despawns or session resets.

The adapter does **not** own CPU/GPU leases, cannot reconstruct an already-destroyed Graph, does not transfer root motion, authored event streams, IK/ragdoll/native solver state or gameplay ECS, and must not run during animation worker dispatch. RTS Model and graph invalidation paths opt in; FPS and editor parity need separate audit. Native compiler/CTest/GPU validation **NOT RUN** for this source-only stage; see [Stage 146 handoff](agent-handoffs/animation-stage146-pre-retirement-playback-rebind-2026-11-19.md).



## Stage 147 host epochs and graph-only publication

The original legacy RTS path had omitted `View::Object::animationModelGeneration` even though `ModelChoice` already tracked a nonzero `view::ModelPublicationEpoch`. It now passes the true published epoch and uses only stable packed ECS IDs for accelerated generic animation; otherwise it retains the legacy path. The pre-retirement named playback seed additionally accepts same immutable Model/same nonzero epoch **graph-only** recompile, not just a new-Model/new-epoch publication. All variants require exact unchanged logical generation and copied rig hierarchy; ambiguous mixed address/epoch cases restart and old Model/Graph never survive the detach. Native tests and end-to-end rendering still **NOT RUN**; see [Stage 147](agent-handoffs/animation-stage147-rts-publication-epoch-and-graph-rebind-2026-11-19.md).



## Stage 148 — universal ECS Graph-only replacement and single source owner

For any host with an existing presentation ECS Graph player, use `aiverse::motionweave::rebindAnimationPresentationGraph(world, entity, *immutableModel, nextGraph)` on the presentation thread after workers join, while the previous CPU Model/Graph are readable. Returns `AnimationGraphRebindResult {replaced, playbackCaptured}`. The new compiled Graph must target the same immutable Model as the current asset. The helper tries to preserve compatible named state/clip phases, fails closed to authored defaults, invalidates old skin pose immediately and clears obsolete montage and graph-notify queues; it never touches simulation ECS or invokes a second evaluator. Only the next normal MotionWeave update evaluates the replaced graph.

Source replacement remains `MotionWeaveSourceBridge::forgetForReimport`, not graph-only helper. A detached same-Model/same-nonzero-epoch graph replacement must have a distinct compiled Graph identity (integer-only old pointer guard, never dereferenced); different Model requires a genuinely advanced nonzero publication epoch and different Model address. The core always invalidates stale poses when a source asset changes, even if the following animation tick is skipped.

Reference clients may either own an independent MotionWeave ECS presentation world or use generic View-owned presentation; they must never evaluate the SAME visual source twice. Host-only physics, gameplay, network or genre actions are adapted outside animation. Editor preview panels with their own standalone generic `render::AnimationGraphInstance` are separate presentation owners, not candidates for an additional ECS player. More details and actual code commits: [Stage 148](agent-handoffs/animation-stage148-genre-neutral-graph-rebind-native-owners-2026-11-19.md). **NOT RUN** native build/CTest/Metal/GL/Vulkan, current green CI, sanitizer, benchmarks.



## Stage 149 — safe source IDs, Model epochs and render LOD invalidation

A `MotionWeaveSourceBridge::sync(world,sources)` source must have a **stable nonzero** `sourceId`. A graph-owned `View::update(objects,dt)` Object with a valid Graph+Model must likewise have a durable `id!=0`; invalid inputs fail before the presentation animation step. Static geometry and **graphless** precomputed pose/baked packets may remain anonymous, because View does not evaluate them. A host must not reuse arbitrary draw-array positions as identity across frames, reimports or entity respawns.

A newly published `Model*` requires a different nonzero `modelPublicationGeneration` from the old one to carry portable named playback on hot reload; same Model/same epoch may retain compatible phase when only the compiled Graph changes. Otherwise the new player safely restarts. `forgetForReimport` is idempotent for duplicate IDs in the SAME detach span, while ordinary `forget` cancels any saved phase. Both View forget APIs erase visual LOD hysteresis for detached source IDs. Cached selected LOD state includes source publication, visual Model address/epoch, selected threshold and level count, and is reset when those tracked revisions change (the entire LOD list is NOT fully fingerprinted). Default attachment persistent IDs incorporate the source Model epoch; explicit IDs are unchanged.

No genre/gameplay/network dependencies were added; there remains a single graph evaluator per source. Full compile, CTest and Metal/GLES/Vulkan runtime checks **NOT RUN** under the agreed feature-first workflow. See [Stage 149 handoff](agent-handoffs/animation-stage149-source-identity-lod-attachment-reimport-2026-11-19.md).



## Stage 150 — visual LOD authoring revisions and immutable socket bindings

Every `View::Object` continues to use exactly one MotionWeave graph/pose writer independently of its chosen visual Model LOD or attachments. The View now invalidates render-only hysteresis based on complete authored LOD configuration revisions (all levels' Model address/epoch, threshold and optional retarget/compiled-plan identity), not just the previously selected level. Comparison snapshots are rebuilt only when the layout changes and contain no live Model ownership.

A generic native host may compile a named or indexed attachment with `aiverse::view::compileModelSocket(rig, epoch, socketName, socketIndex)` at asset publication time, then share that immutable `CompiledModelSocket` through appended `ModelAttachment::compiledSocket`. The View's revision-aware `composeAttachment` checks exact rig/epoch/authored selector; expired compiled sockets fail closed and require recompilation. Legacy uncompiled sockets and root attachments remain available. Host owns CPU Model/GPU retirement and must not mutate a published Model in place without a new epoch. The animation core has no FPS/RTS/RPG dependencies.

See [Stage 150 handoff](agent-handoffs/animation-stage150-complete-lod-signatures-compiled-sockets-2026-11-19.md). **NOT RUN:** compiler, native CTest, current passing CI, real Metal/Vulkan/GLES visuals, sanitizers or benchmarks pending the user's feature-first single validation pass.



## Stage 151 — authored presentation binding transaction

A universal native project can now use `AppearanceAssetRegistry::Asset::compilePresentationBindings` to compile a whole set of visual Model LOD retarget plans and socket bindings against immutable published source/visual snapshots. Successful `CompiledPresentationBindings::apply(View::Object&)` installs the configuration atomically after validating the source Model epoch and authored socket selectors. Invalid input preserves the last-good candidate; old compiled sockets are removed on complete replacement instead of silently carrying incorrect bone indices across a reimport.

This is **publication/authoring only**, not another animation evaluator, gameplay action system, RHI allocation or host-specific rule. The single MotionWeave presentation owner continues updating the source Graph, while the generic View uses the precompiled LOD and sockets for rendering. Caller retains CPU Model/Graph/GPU resource lifetime responsibility. Detailed APIs, exact code commits and final validation cases are in [Stage 151 handoff](agent-handoffs/animation-stage151-transactional-presentation-authoring-2026-11-19.md). Local native compiler/CTest/real Metal-GLES-Vulkan GPU/sanitizers/performance are **NOT RUN**.



## Stage 152 — published appearance packet ownership across native projects

A published `AppearanceAssetRegistry::Asset` can now build an entire `View::Object` transactionally with optional precompiled LOD/socket bindings using `packetWithBindings`. LOD importer-name matching is explicit via `VisualLodRequest::matchByName`, validated by the normal generic retarget compiler; it never guesses a bone index or morph map. Invalid binding or Model epoch leaves the previous output unchanged.

Reference native FPS presentation consumes this common published packet path for real remote character renders but still owns and evaluates the single animation graph in its own presentation ECS. Its precomputed pose/bake packet **clears the graph** before passing through `View::beginPreparedFrame`/submit, so generic View cannot evaluate it twice. Genre-specific decisions remain exclusively in the client. Editor preview, specialized host assets and full real child-model import/bundles remain partial. See [Stage 152 handoff and status table](agent-handoffs/animation-stage152-native-appearance-packet-integration-2026-11-19.md). Native compiler, CTest, CI green for new head, actual Metal/GLES/Vulkan and sanitizer/performance validation **NOT RUN**.



## Stage 153 — imported attachments safely share one generic animation owner

The genre-neutral `ImportedAttachmentAuthoring` helper in `engine/view/ImportedAttachmentAuthoring.h` makes real `ModelAssetProvider` child Model import, static `AppearanceAssetRegistry` publication and compiled socket composition available to any native project. Its move-only `Prepared` keeps source/child published appearance snapshots and short-lived `ModelResourceCache::acquireModelRead` leases for source, visual LOD and child Models until synchronous render submission. Host still owns GPU barriers, old model retirement and source generation changes.

Choose `ViewEvaluates` when `View::update` is the ONE animation graph owner, or `HostPrecomputed` when another presentation World has already evaluated a Pose (its render packet graph is reset to prevent a second evaluator). Animated child Graph assets are deliberately rejected from the *static* attachment pathway and need independent source IDs/updates if animated. Current editors and reference clients are not yet wired to import an authored child set using the helper; Stage 154 will address that separately. [Stage 153 handoff](agent-handoffs/animation-stage153-imported-child-attachments-read-leases-2026-11-19.md) contains API, source SHAs, readiness matrix and test backlog. **NOT RUN:** native C++ build/CTest/current CI green, Metal/Vulkan/GLES GPU, sanitizers and performance.



## Stage 154 — ModelView real imported child attachment workflow

The reusable `ImportedAttachmentAuthoring` is now called by the native `aiverse_modelview` asset preview. Real child Model files and the parent share the same `ModelAssetProvider` owner, while published Appearance snapshots provide materials/epochs and compiled named sockets provide bone placement. CLI `--attach child.glb@SocketName` and `--attachments-file config.json` are backed by an Attachments ImGui window with add, toggle, Detach, Rebuild and Save. Owner thread applies edits between completed frames, with short-lived source/child read leases held until synchronous `View::submitOne`.

ModelView's existing Animator is the only animation writer; its already evaluated Pose is handed to generic View with `HostPrecomputed`, so the View will not schedule an extra MotionWeave Graph. No host/game genre types entered MotionWeave or engine View. This is not yet full AIVERSE Studio authoring and does not hot-reimport changed on-disk child bytes; provider GPU cache and deferred retirement remain host-owned. [Stage 154 handoff](agent-handoffs/animation-stage154-modelview-attachment-workflow-2026-11-19.md) / [user workflow](modelview-attachments.md). Native build/CTest/actual GPU Metal/Vulkan/GLES/sanitizers/benchmarks **NOT RUN**, deferred to genuine feature freeze.



## Stage 155 — Studio attachment authoring, resource-isolated reimport

AIVERSE Studio now opens a dedicated `MotionWeave — Attachments` authoring window from Modeling and Animation Graph/Rig toolbars. It uses the same genre-independent `ImportedAttachmentAuthoring` Model and named-socket compiler as native ModelView, but has its OWN provider-owned cache for real imported parent/child models. It never imports gameplay actions or borrows a different editor preview panel's directly-owned Model; the existing editor GraphInstance remains the only preview animation writer.

`Reimport & Publish` rereads on-disk Model sources into a complete separate staged GPU resource owner before committing the new snapshot. Errors keep the previous published asset session; a full Editor GPU idle barrier gates destruction of the old provider. This is intentionally blocking, not seamless/nonblocking, and is currently a **file-layout and socket-validation editor**, not a second 3D preview or a live scene/prefab operation. Further visual integration, ModelView source reimport, retarget/morph authoring and backend validation remain pending. [Stage 155 handoff](agent-handoffs/animation-stage155-studio-attachment-staged-reimport-2026-11-20.md) / [UI guide](studio-attachment-authoring.md). **NOT RUN:** native compiler/CTest/CI green/Metal-Vulkan-GLES/ASan-TSan/benchmarks.


## Stage 156: real Studio 3D preview, deferred attachment owner generations

AIVERSE Studio's attachment authoring window now owns a true offscreen
parent+child model viewport using the genre-neutral `ImportedAttachmentAuthoring`
pipeline and a render-only `View`. It samples the first imported clip in a
temporary preview pose, composes named bone sockets from the same published
source Model, and draws without creating another MotionWeave graph owner.

New source/child provider generations may publish immediately after successful
authoring; previous and failed scratch providers remain retained until a safe
full GPU-idle cleanup at the bounded queue threshold/shutdown. These delayed
owners reduce per-reimport stalls, but still require full idle for actual
provider shutdown and can increase peak memory; do not call this true
fence-based or zero-stall retirement. See
[Stage 156 handoff](agent-handoffs/animation-stage156-studio-attachment-preview-deferred-retirement-2026-11-20.md).
Real C++/CTest/GPU/visual/sanitizer/benchmark verification remains **NOT RUN**.



## Stage 157 — native whole-source hot reload without a second animation owner

Stage 157 adds a ModelView example **complete on-disk source generation replacement** while leaving the common genre-independent MotionWeave/AnimationGraph/ECS/View code unchanged. A staged native ModelAssetProvider owns all newly imported parent+child Models; the host creates new Animators bound to those Models and fresh compiled sockets against newly published source/child appearance snapshots. Valid compatible named clips preserve normalized phase via the existing `Animator::seek` API. Only after all imports/compilers succeed does ModelView publish the new scene. Failed candidates are held in a GPU-safe old-owner queue pending definitive completion. The Viewer emits precomputed poses through the same single-writer generic View pipeline.

User controls: ModelView **Reload model files** (full new GPU/CPU asset generation), cached-only **Rebuild**, opt-in `--watch-model-files` / **Watch source files** polling. Retired Animator/Model/attachment owners are retained until a full GPU idle check at an advisory queue cap of eight or shutdown; not a GPU token-based asynchronous retirement API, and peak resource use can increase. Studio's Stage 156 3D preview remains separate. See [Stage 157 handoff](agent-handoffs/animation-stage157-modelview-atomic-model-file-reimport-2026-11-20.md). Native compiler/CMake/CTest/GPU Metal-Vulkan-GLES/ASan-TSan/benchmarks **NOT RUN**, per requested feature-first workflow.



## Stage 158 — universal authored attachment layout shared by Studio and ModelView

`view::AttachmentLayoutCodec` now supplies one genre-neutral attachment document format for parent path, real imported static child Model/socket/visibility and both full affine transforms. Legacy JSON (no matrices) uses identity; invalid or non-affine transforms reject transactionally. Both Studio and native ModelView use the shared parser/writer and `ImportedAttachmentAuthoring::childFromLayout` for identical runtime placement. Studio supports editing 3x3 basis axes and child offsets; ModelView supports per-socket position and can open its parent directly from saved JSON. The parent Animator remains the only graph/pose writer for static attachments.
 
These documents are **ready for future scene/prefab serialization**, but do not yet connect to live ECS entity creation/destruction or gameplay scenes. Stage 158 GPU audit also confirms true token-complete provider retirement is unfinished: existing ModelResourceCache and image rollback shutdown depend on full device GPU idle; no arbitrary token is sufficient proof to skip that. See [Stage 158 handoff](agent-handoffs/animation-stage158-universal-attachment-layout-transform-2026-11-20.md). Native compiler/CTest, real Studio/ModelView/Metal-Vulkan-GLES, sanitizer and benchmarks **NOT RUN**.


## Stage 159 — real presentation ECS attachment scene/prefab bridge

`view::EcsAttachmentScene` provides a genre-independent authoring-to-runtime bridge from pointer-free `AttachmentLayout` JSON to actual child Entities of an **isolated presentation** `aiverse::World`. Parent remains externally owned; Root and Child components record unique instance revision, generation-aware parent Entity, published source Model epoch, slot ordering, Model path, socket and full local/child transforms. `publish` compiles all published child Model sockets and validates CPU leases BEFORE world structure changes; replacement removes only old owned child handles. `restore` rehydrates fresh child Entities, `save` preserves only authored data, `forget` releases exact owned children and `valid/prepare` fail closed on stale ECS handles. The renderer continues using ONE host-computed parent pose plus existing static attachment composition; no duplicate graph writer.

Studio now uses this ECS World/bridge directly in its native attachment 3D preview and staged reimport, rather than only keeping a plain compiled vector. Full SceneLoadCoordinator/game-scene/prefab activation integration is still missing because `engine/scene` is absent from this divergent feature branch; review the peer scene agent's API before integrating. Animated child graphs, true ModelAssetProvider GPU token shutdown and advanced morph retarget remain open. See [Stage 159 handoff](agent-handoffs/animation-stage159-presentation-ecs-attachment-prefab-restore-2026-11-20.md). Native C++/CTest/Metal-Vulkan-GLES/sanitizer/failure injection **NOT RUN**.



## Stage 160 — source-neutral stable child IDs and presentation scene ownership

AIVERSE attachments now support optional explicit stable prefab child IDs in the shared `AttachmentLayout` JSON document (<=128 characters, unique per parent); old ordinal-only documents remain valid. Runtime ECS `SceneAttachmentChildComponent` records the authored ID, and `EcsAttachmentScene::resolveChild` revalidates parent/child generation/revision on each lookup instead of exposing a stale saved Entity index.

`SceneAttachmentHost` maps native `sceneId + prefabObjectKey` to independent cosmetic presentation ECS attachment instances. It handles activation, replacement, exact owned-child removal, unload of a complete scene and pointer-free per-scene export; `prepare` still returns a HostPrecomputed graphless pose packet and ModelReadLeases. Studio reuses the adapter for its 3D preview, and ModelView retains stable IDs through JSON Save/Load. Genre-specific simulated ECS, damage and networking remain outside MotionWeave.

A reviewed integration with the peer scene loader `SceneLoadCoordinator::SpawnPrefab` (branches `feature/scene-runtime-multiworld` / `feature/ecs-static-prefab-scene-integration`) is still required and NOT implemented in this animation branch. Independent animated child sources, true ModelAssetProvider GPU-token shutdown and advanced morph/retarget remain pending. [Stage 160 handoff](agent-handoffs/animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md) / [host API guide](scene-attachment-host.md). Native compiler/CTest/Metal-Vulkan-GLES/ASan-TSan/performance **NOT RUN**.



## Stage 161 — independent child MotionWeave graphs, one player per source

The stage implements a genre-neutral `AnimatedSceneAttachmentSources` service over the existing `MotionWeaveSourceBridge` and `stepAnimationPresentation`. Its own presentation World owns one GraphInstance for EACH animated child stable `scene/object/childId` identity. Static children remain compiled socket draws using their parent's already computed pose; an authored `animated=true` child is excluded from that static packet and requires a published external `animationGraph` and stable nonempty child `id`. All child source Model/Graph/epoch validations and parent named socket compilation run before the independent child evaluator is advanced.
 
The parent pose and bone matrices place the independent child's world transform; the child supplies its own evaluated local Pose, and the returned `View::Object.graph` is reset so a generic View never evaluates it a second time. Studio's real attachment preview imports/compiles child Graph assets and steps them once in pre-draw owner update before graphless render submission. ModelView retains authoring fields but does not yet support an independent child player; it fails closed. Full native scene-loader integration, tokenized provider shutdown, advanced morph/LOD parity and actual Metal/Vulkan/GLES/CMake/CTest/ASan-TSan testing remain outstanding. [Stage 161 handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md).

## Stage 180 — generic compiled facial Control Rig and native IK

The opt-in `"engine/animation/FacialControlRig.h"` /
`AnimationFacialControlComponent` maps exact importer-authored
mesh-local 64-channel morph names to named expression controls, applies
per-character half-life smoothing, and executes existing
`render::AnimationRig` Aim/AngularLimit/IK AFTER one Graph/Animator
pose evaluation, BEFORE final matrices. Facial controls are a separate
presentation ECS component (do not require changing existing Graph
parameter layouts). Invalid/re-imported rig metadata and missing
controls fail without mutating the incoming pose. Morph-only sources
may use MotionWeave workers; any native face IK or host callbacks are
routed through the serial owner-thread barrier.

See [facial authoring and runtime](motionweave-facial-control-rig.md) and
[Stage 180 handoff](agent-handoffs/animation-stage180-facial-control-rig-2026-11-20.md).
This is source implementation only: CMake/CTest, real face/glTF/FBX
assets, Metal/Vulkan/GLES GPU parity and performance **NOT RUN**.


## Stage 183 — native facial preview from Studio-authored JSON (source-only)

`<aiverselib/animation/FacialPreviewSession.h>` exposes a reusable
host-facing compiled expression/IK session that consumes **real**
imported Model morph/rig signatures and the existing HTML5 Studio
`facial-rigs/<name>.json` v1 schema. It applies after a single parent
Animator/Graph pose and before final matrices; invalid presets or
model revisions retain the last-good cosmetic rig or fail closed.
`apps/modelview/main.cpp` now supports `--facial-rig <path>`,
`--face jawOpen=0.8` and `--face-vector lookAt=2,1,3` for native
GPU ModelView preview and project-saved-JSON hot reload.

**Not yet** integrated into the embedded HTML5 Studio Scene viewport:
`feature/studio-html5` is still separate, and the native IPC and
Model/GPU lifetime coupling are Stage 184 work. Details:
[Stage 183 native preview](motionweave-facial-native-preview.md) and
[Stage 183 handoff](agent-handoffs/animation-stage183-studio-facial-native-preview-2026-11-20.md).
No native CMake/CTest/Metal/Vulkan/GLES validation has been run.

## Verification, locomotion review and CPU cost (2026-11-22)

Branch `agent-motionweave-next`. Everything below was **run**; the review is
a CPU stick-figure render of the evaluated MotionWeave pose, **not** a Metal
render of a skinned character (no humanoid with locomotion clips ships in the
repo, so the test character is procedural).

### Test character and tools

- `tests/animation/HumanoidLocomotionFixture.h` — 53-bone procedural humanoid
  (+Y up, +Z forward, metres) with planted-foot clips authored through
  `solveTwoBoneIK` and baked to 30 Hz keys: `Idle` 2 s, `Walk` 1.2 m/s,
  `Run` 3.0 m/s, `Stop` (walk → idle in 0.36 m), `TurnLeft` (90° in place),
  their in-place `*_IP` variants and optional `GaitNN` speed variants.
- `aiverse_motionweave_core_tests` (CTest `motionweave_core`, 292 checks):
  sampling, world matrices, blend/mask, sync groups, morph-free path,
  two-bone IK, rig world reuse, motion matching and a foot-slide regression.
- `aiverse_motionweave_locomotion_review <dir>` (tools) drives the humanoid
  through idle → start → walk → run → walk+turn → stop → idle → turn in place
  with three graphs (`blend`, `blend-sync`, `motion-matching`) and reports
  planted-foot slide, pops (joint acceleration, rotation step) and broken
  poses; `tools/animation/motionweave_review_render.py <dir>` renders
  contact sheets, footprints, slide plots and an MP4.
  Committed output: `docs/qa/motionweave-locomotion-2026-11-22/`.
- `aiverse_motionweave_humanoid_bench` (tools): per-character CPU cost for
  1/10/100/1000 actors and the building blocks; uses only graph features that
  exist on `origin/main`, so the same source measures before/after.

### What the review found and what changed

| Finding | Evidence (review) | Fix |
|---|---|---|
| Motion matching wrapped one-shot clips (Stop) to frame 0 | 35 cm foot teleport in one frame, pop 1298 m/s² | database clips accept `{"clip":"Stop","loop":false}`: clamp at end, search on arrival, trajectory holds the final root; pop now 293 m/s² (= steady run) |
| Motion matching could not pick a turn in place (trajectory had positions only) | `TurnLeft` never chosen; 0.157 m/s slide while turning | trajectory samples accept `"facing":"<vector param>"` (+`facingWeight`, `facingAxis`); turn-in-place slide 0.048 m/s |
| Motion matching faded in from the bind pose at spawn | straight-leg frame, ankles below ground | first selection starts on the match (below-ground frames 27 → 7) |
| Walk/run blend clocks were independent | planted-foot slide at w=0.5: 0.94 m/s | blend `"sync": true` locks normalized phase, blended cycle rate: 0.31 m/s (core test) |

Not fixed (documented limitations, visible in `slide.png`):
state-machine crossfades still start from a **frozen** outgoing pose, so
idle→walk starts (≈0.87 m/s slide for 0.25 s) and walk→stop slide; walking
turns slide (no turn clips / foot locking); motion matching with only walk and
run speeds slides while the host speed is between them (data coverage), and
the idealized 0.6 s lookahead starts stepping before the host moves. No
foot-lock IK pass exists yet; that and inertialized state transitions are the
next steps.

### CPU cost (Apple M3 Ultra, Release, 53 bones)

Interleaved A/B of the same bench source against the engine before
(`4a9d15b52` = `origin/main` + sync groups) and after (`d635f8295`) the
optimisations; the Mac was shared with other agents (load 7–15 on 28 cores),
numbers are medians of 3 runs and repeated within ~5 % across rounds.
µs per character per frame, sequential `MotionWeave::update`:

| scenario | 1 | 10 | 100 | 1000 | scheduler, 1000 actors |
|---|---:|---:|---:|---:|---:|
| clip | 2.61 → 2.06 | 2.97 → 2.34 | 3.23 → 2.67 | 4.73 → 3.90 | 0.65 → 0.57 |
| blend walk/run | 4.73 → 3.35 | 5.15 → 3.75 | 5.62 → 4.27 | 7.25 → 6.44 | 1.04 → 0.88 |
| blend + 2-leg IK | 6.83 → 4.72 | 7.25 → 5.17 | 7.69 → 5.92 | 9.48 → 8.37 | 1.34 → 1.13 |
| motion matching (814 frames) | 6.62 → 3.77 | 7.00 → 4.11 | 7.44 → 4.33 | 8.76 → 5.49 | 1.18 → 0.74 |
| motion matching + IK | 8.50 → 5.23 | 9.10 → 5.68 | 9.56 → 5.94 | 10.88 → 7.12 | 1.47 → 0.97 |

Building blocks (µs/call): `MotionDatabase::search` 814×24 15.8 → 1.48;
graph update+evaluate: clip 1.60 → 1.19, blend 3.75 → 2.35, blend+IK
5.83 → 3.74, MM 5.64 → 2.75, MM+IK 7.65 → 4.17. Unchanged blocks:
`sampleClip` 0.66, `worldMatrices` 0.40, `solveTwoBoneIK` 0.30, `bindPose` 0.26.

Hot spots and what was done:
1. MM query extracted the **full** feature vector (2–8 extra pose samples,
   trajectory part discarded) → pose part only, per-thread scratch.
2. MM search: per-frame `std::vector` rows, double sums → contiguous
   `sqrt(weight)`-scaled float matrix, 4-wide sums, early exit (10×).
3. `NodePose` carries 64 morph weights (296 B vs 40 B of t/r/s): every pose
   copy and blend touched them → `AnimationGraph::morphFree()` graphs copy
   and blend only t/r/s (exact: internal poses keep bind weights).
4. Each rig unit rebuilt all world matrices → reuse after `twoBoneIK`.
5. Tried and **reverted**: hand-written affine `worldMatrices` product was
   slower (0.44 vs 0.39 µs) than the vectorised generic one.

Remaining cost at 1000 sequential actors is memory traffic (per-actor poses
and instances; µs/char grows ~1.6× from 1 to 1000 actors) plus the ECS layer
(defaults map copy per entity per frame, final `worldMatrices`). Use the
persistent `MotionWeaveScheduler` (7 workers here): 1000 MM+IK characters
cost ≈1 ms/frame.

### Commands

```sh
cmake --build <build> --target aiverse_motionweave_core_tests \
  aiverse_motionweave_humanoid_bench aiverse_motionweave_locomotion_review
ctest --test-dir <build> -R "motionweave|animation"      # 42/42 passed
<build>/aiverse_motionweave_humanoid_bench --repeats 3
<build>/aiverse_motionweave_locomotion_review /tmp/mw-review
python3 tools/animation/motionweave_review_render.py /tmp/mw-review
```
