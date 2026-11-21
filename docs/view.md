# AIVERSE View: genre-independent scene

The canonical engine-level scene presentation class is
`aiverse::View` (alias `aiverse::view::View`),
available from `<engine/view/View.h>` and CMake
`AIVERSE::View` (legacy spelling `AIVERSE::view`).

It is not an RTS view, FPS view, map editor view or RPG view. All
hosts project their objects into generic scene data. The core
does not receive game orders, movement intents, network actors,
weapon IDs, AI states or gameplay module classes.

## Universal model and mesh packets

```cpp
#include <engine/view/View.h>

aiverse::View scene;
std::vector<aiverse::view::Object> objects;

aiverse::view::Object object;
object.id = stableSourceId;
object.generation = sourceGeneration;
object.draw.model = model;
object.draw.world = modelToWorld;
object.graph = compiledGraph; // optional
object.parameters.set("speed", animationSpeed);
objects.push_back(std::move(object));

// Every update uses a separate presentation-only ECS World:
scene.update(objects, frameDt);
scene.submit(renderer, objects); // inside an initialized renderer frame
```

For ordinary meshes:
```cpp
aiverse::view::MeshObject mesh;
mesh.mesh = uploadedMesh;
mesh.world = modelToWorld;
mesh.color = {1, 1, 1, 1};
scene.submitMesh(renderer, mesh);
```

`View::submitOne` forwards one generic model packet; it can accept a
host-precomputed node pose, baked GPU sample and all ModelDraw styling.
Models, meshes and material pointers are borrowed; host must keep them
valid until submission completes.

The animation side is `MotionWeave` and its generic
`MotionWeaveSourceBridge`. No genre-specific class is required,
and the same worker scheduler/animation graphs serve every project.
The renderer is still `Renderer3D`; this new View does not invent
a second material or GPU backend.

## Dependency direction

```text
Game, Editor, UI, Network, Simulation host adapters
                    ↓
             AIVERSE View
                    ↓
      MotionWeave + Renderer3D / RHI
```

Neither `engine/view` nor MotionWeave may import `modules/rts`,
`modules/fps` or any other gameplay module. World simulation remains
authoritative and separate from presentation.

## Migration status from legacy RtsView

The old `game::RtsView` is a compatibility adapter, not the canonical
View. A subset of simple AnimationGraph objects already delegates
pose evaluation and styled ModelDraw submission to `aiverse::View`;
complex procedural/contact IK and RTS-specific object decisions remain
in the game adapter until parity is established.

The FPS client uses the SAME View for character models and primitive
meshes, proving the API has no genre-specific branches. Further
migration is needed for generic layers/camera, terrain, roads, fog
and field/planet rendering that are historically entangled with
the RTS module. Do NOT rename the legacy class without removing
its RTS model and Rules dependencies.

## Deferred validation

Tests `view_genre_independent` and `view_genre_boundary` are registered,
alongside MotionWeave tests. **NOT RUN** by user's request;
CMake, CTest, TSAN, FPS/RTS screenshots and performance still need
real execution before production is claimed ready.

Every stage must update `AGENTS.md` and append a separate committed
entry in `docs/agent-handoffs/animation-universal.md`.

## Camera API migration

The engine renderer's target/orbit camera is now named
`aiverse::render::Camera3D`. `render::RTSCamera` remains an
exact `using` alias for source compatibility: no duplicate data,
no copy/conversion and no change to the existing camera math.

`View::beginScene(renderer,camera,width,height)` is a
genre-independent entry point to Renderer3D's existing
beginScene pipeline, so a client/editor no longer needs to
refer to an RTS-named camera type. The render backend, viewport,
shadows and original camera parameters are unchanged.

### Generic immediate geometry

`view::LineObject` and `view::TriangleObject` are unowned
scene-space geometry packets. Static `View::submitLine(renderer,line)`
and `View::submitTriangle(renderer,triangle)` forward their
vertices/colors to the existing Renderer3D pass. They do not
know what any diagnostic, selection, fog blip or game event
means; the host decides which geometry to submit.

The transitional game adapter already routes selected projectile
tracers and fog-of-war blip triangles through these shared packet
methods. All other legacy geometry remains valid on the old
immediate-mode renderer until its own host extraction.

## Mixed scene packet stream

Use `view::ScenePacket` and `view::ScenePacketBatch` when a host
must submit a collection of heterogeneous model, mesh, line
and triangle draw packets in the **exact original order**:

```cpp
aiverse::view::ScenePacketBatch packets;
packets.reserve(expectedDraws);
packets.add(modelObject);
packets.add(meshObject);
packets.add(lineObject);
scene.submitPackets(renderer,packets);
```

A ScenePacket is a tagged variant over the existing public
`Object`, `MeshObject`, `LineObject`, and `TriangleObject`.
The engine View dispatches one item at a time through
the same `submitOne` / `submitMesh` / `submitLine` /
`submitTriangle` paths as direct callers. It does not
sort, reorder, re-evaluate animations or change internal
Renderer3D batching. The host owns all borrowed model,
mesh and material resources for the duration of submit.

## Surface sampling

The generic header `<engine/view/Surface.h>` exposes
`view::composeSurfaceHeight(x,z,base,layer)` and
`view::sampleSurface(x,z,normalStep,heightSampler)`.
These are allocation-free templated algorithms for combining an
arbitrary host-authored base surface with a height modifier,
and for computing a visual terrain normal from five height
samples. No MapGrid, roads, physics, genre or entity type
appears in this engine-level API.

The historical game adapter's `groundAt` now composes
its own map-height policy with its own road surface policy
through that helper, retaining the previous values and
placement semantics. Future hosts may supply their own
heightfields, planets projected to local surfaces, mesh
raycasts or procedural floors without modifying View core.
This query is **presentation-only**, not a replacement
for deterministic character or vehicle collision.

## External scene layer providers

`engine/view/SceneLayerProviders.h` introduces
`ViewLayerProviders`. A host registers opaque named callbacks
and decides when they are enabled. `collect(ViewLayerQueue&,
ViewFrameInfo)` schedules frame work in registration order.
`ViewLayerQueue::flush()` invokes it synchronously on the
render thread, before any borrowed model/terrain/camera state
may expire.

The generic layer API knows only names, frame seconds, dt,
revision and presentation callbacks. No genre-specific
resource schemas live in engine View. The compatibility
game-view host now registers its separate path and
shoreline presentation passes as outside providers while
retaining their existing geometry algorithms and order.

### Fully prepared draw packets

Use `aiverse::View::submitModel(renderer, draw)` for a
precomputed `render::ModelDraw`. Unlike `View::submitOne`,
this does not read animation ECS or require a source ID.
It forwards existing staged/LOD model variants, baked samples,
per-node procedural poses, attachments, materials and flags
through the same Renderer3D model pipeline.

The old game-side draw implementation now forwards **all**
base model and attachment ModelDraw submissions this way,
not only the simple AnimationGraph subset. Complex authored
animation still runs inside the legacy host adapter until
its state is migrated; it is **not** recomputed by the View.
Model and material memory is borrowed only for the synchronous
submission.

## Reusable visibility field

The previously game-local visual fog implementation has moved to
`engine/view/VisibilityField.h/.cpp` under
`aiverse::view::VisibilityField` and
`aiverse::view::VisibilityLook`. It accepts only a host-provided
0/1/2 byte visibility mask, visual settings, elapsed presentation
time and a height sampler for the GPU sheet. Grid blur/fade,
visibility texture upload and draw are reusable in strategy,
stealth, exploration, spectator views and editors alike.

`modules/rts/view/RtsFog.h` remains a compatibility-only
header: `game::FogOfWar` and `game::FogLook` are aliases to the
engine types and `game::parseFogLook` forwards to
`view::parseVisibilityLook`. Existing RTS project settings and
editor behavior can retain their original names.

Link the optional compiled presentation effect target
`AIVERSE::view_visibility` when using the standalone
visibility system. This is a render effect, not the
authoritative visibility or enemy-detection algorithm.

### Portraits, previews and projectiles

The legacy game's portrait off-screen renderer now enters its
existing render-target frame through `View::beginScene` and
submits material previews and CPU poses through
`View::submitModel`. The view does not own color/depth
render targets or change graphics pass ordering.

Ordinary flat-map, procedural and planetary projectile meshes
are submitted as generic `view::MeshObject` packets through
`View::submitMesh`, including their existing transforms,
unlit flags and colors. `submitMesh` and `submitMeshes`
are stateless static entry points; legacy object calls remain
source-compatible. This is a **render submission migration**:
the game host still owns projectile gameplay, planet tangent
frames, and previews' simulation-specific data.

### VisibilityField buffer API and ownership

The visibility effect accepts any contiguous byte mask without
allocating a temporary `std::vector`:

```cpp
#include <engine/view/VisibilityField.h>
aiverse::view::VisibilityField visibleLayer;
visibleLayer.init(device);
visibleLayer.setLook(viewLook);
visibleLayer.build(device,width,height,heightSampler);
visibleLayer.update(std::span<const uint8_t>(mask.data(),mask.size()),
                    width,height,viewSeconds);
visibleLayer.draw(renderer,viewSeconds,isLowTier);
// On shutdown, before device destruction:
visibleLayer.shutdown();
```

Existing vector-based `update(vector,...)` calls remain supported
and forward to the same zero-copy span update implementation.
The host is responsible for mask semantics and lifetime during
the synchronous update. This is visual fade/blur and GPU
rendering only, never simulation-side visibility authority.

## Generic circular overlays

`view::CircleXZObject` and `View::submitCircle` send a
world-space ring directly to Renderer3D. The scene packet
variant also accepts `CircleXZObject`, preserving ordered
batch submission alongside models, meshes, lines and triangles.
The host decides whether rings represent selection,
markers, shadows or visual debugging. No game state
enters the View.

The game compatibility view routes previously hard-coded
radar marker rings and simple projected circle shadows
through these same generic packets.

## Visibility and scene-layer lifecycle

The reusable `view::VisibilityField` has a **visual-only**
0/1/2 visibility mask supplied by the host. Import
`<engine/view/VisibilityField.h>` and separately link
`AIVERSE::view_visibility` when the effect is needed.
Call `init(device)`, `setLook()` (or
`parseVisibilityLook` from generic JSON), `build(device,
w,h,heightSampler)` when the ground geometry changes,
`update(mask,w,h,seconds)` each presentation frame, and
`draw(renderer,seconds,lowTier)` in the desired
render pass. Call `shutdown()` before releasing its GPU
device. The field stores GPU textures and must remain on
the render thread; callers own the mask source and whether
terrain shader fog or a separate sheet is drawn.

`ViewLayerProviders` is intentionally only an ordered
registration/composition seam. Host-owned providers
may add terrain, visibility, roads, planet or post layers
without importing any game types into `engine/view`.
A `ViewLayerQueue` must be **flushed synchronously**
before returning from the frame scope; callbacks frequently
borrow textures, camera matrices and terrain references.

## Shared camera-side frustum (flat terrain and planets)

The header-only `engine/view/Frustum.h` provides
`view::SideFrustum::fromViewProjection(vp,normalized)`,
`visibleAABB(min,max)` and `visibleSphere(center,radius)`.
It extracts only four camera side planes, avoiding near/far
clip-convention differences. Flat terrain chunk culling uses
raw planes and AABB positive vertices; planet body and
chunk culling uses normalized planes and sphere radius.
The host alone owns quadtree LOD, horizon occlusion and
rendering policy. All CTest validation remains **NOT RUN**.

## Spherical horizon culling

`engine/view/Horizon.h` exports
`view::sphericalCapHiddenByHorizon(cameraLocal,
occluderRadius,capAxis,cosCapHalfAngle,maxSurfaceRadius)`.
It is a reusable floating-point visibility test for
any curved surface, sphere chunks, visual planet and
orbital scene; it knows nothing about game planets,
RTS/FPS or data models. The host supplies the camera
in the body's local coordinate frame and its precomputed
angular cap and radius bounds.

The old planetary renderer delegates its horizon
angle comparison to this helper, while retaining
its LOD/build budget, face tree and graphics pipeline.
Inside the occluder or with invalid occluder radius
the helper returns "not hidden" conservatively.
Test `view_spherical_horizon` is deferred (**NOT RUN**).

## Generic rectangular quad-grid geometry

`engine/view/GridGeometry.h` exports
`view::appendQuadGridIndices(vector<uint32_t>&,columns,rows)`,
a safe vertex-index tessellation primitive for regular 2D grids,
heightfield patches and cube-sphere face patches. It appends
triangles using the same a-d diagonal and winding as the
existing flat and curved terrain builders. The terrain
host retains its own LOD skirts, color sampling and morphing;
planet rendering retains outward normal/winding correction,
surface atlas mapping and horizon policy. The generic
algorithm only receives column/row counts and a vector of
indices, with no game-map, physics, height or render rule.
Deferred CTest: `view_grid_geometry` (**NOT RUN**).

### Per-frame geometry visibility caching

`SideFrustum::fromViewProjection` now caches all four
plane-normal lengths at camera setup. Sphere culling
uses those cached values instead of computing four
square roots for **every** planet/body/chunk.
`SphericalHorizon::fromLocalCamera(cameraLocal,occluderRadius)`
similarly caches eye direction and horizon base angle
**once per spherical body**; `hidden(capDir,cosCap,maxRadius)`
tests many chunks with no redundant camera-length
calculation. The original single-call horizon function
remains as a compatibility helper.

The quad-grid tessellator uses widened 64-bit intermediate
index arithmetic for large yet valid 32-bit grids.
These are static performance/correctness changes; all
benchmarks, CTest and graphics validation remain **NOT RUN**.

## Shared oriented triangle normal / winding

`engine/view/TriangleFacing.h` provides
`view::orientTriangleTo(a,b,c,outwardHint)`, returning
a normalized face normal, flip-winding flag and degenerate
triangle status. Road meshes use it to orient local
triangle vertices and UVs; spherical face meshes use
the same test before correcting the patch's triangle
indices. The host retains its material policy, UV
mapping, face basis and any geometry topology.

It has no genre, game geometry schema, simulation,
AI or physics dependencies. Deferred CTest:
`view_triangle_facing` (**NOT RUN**).


## Unified mixed-packet animation update

Hosts that build one ordered `view::ScenePacketBatch` can now use that
same batch to drive MotionWeave before submitting the renderer commands:

```cpp
aiverse::View scene;
aiverse::view::ScenePacketBatch batch;
batch.add(animatedObject); // view::Object with stable ID + optional graph
batch.add(staticMesh);     // view::MeshObject
batch.add(debugLine);      // view::LineObject

scene.update(batch, frameDt); // evaluates Object packets once
scene.submitPackets(renderer, batch); // keeps mixed draw order
```

`View::update(const ScenePacketBatch&,float)` extracts only Object packets
into reusable frame-local scratch storage. It keeps source ID/generation,
AnimationGraph, budget and visibility semantics identical to the original
`update(span<const Object>,float)`; other packet types do not become
animation sources. The bridge synchronizes missing sources, so a source
removed from the batch will leave the presentation World on the next update.
Do **not** also call `update(objects,dt)` in the same frame: that would
advance animation twice. All packet resources remain host-owned until
synchronous render submission. This is an API convenience, not a new
render pass or a change to authoritative gameplay.

## RTS single-writer animation boundary

The legacy RTS scene adapter selects only simple, pure pose graphs
(no procedural bones, contact IK, custom traits, attachments, stage/LOD
switches or host-injected animation overrides). It stages those sources
in one `ScenePacketBatch` and calls `View::update(batch,dt)` once
before ordinary scene rendering. View's presentation World is the
single evaluator for each selected source.

`RtsAnimate.cpp` then obtains the evaluated pose from View. When
`EntityAnim::genericViewPose` is true, legacy `Animator::update` and
bind-pose fallback **must not execute**, even if the model has imported
clips. Objects that fail eligibility or pose validation keep the existing
legacy graph/clip/procedural path, including terrain contact IK. RTS
still supplies material, world transform and draw style to View's
`submitOne`; no second MotionWeave evaluation is requested during
submission. This is a presentation-only migration: simulation,
network/save formats and deterministic checksums are unchanged.

**Verification pending:** the integrated RTS regression suite,
animated-model visual comparisons and macOS Metal path have not been
executed for this stage.

## RTS animation owner fallback verification (Stage 79)

When a pure graph object becomes ineligible for the generic View path,
`RtsAnimate.cpp` validates the current frame's generic pose before
recording the owner transition. If it cannot consume a valid pose, it
reinitializes **both** legacy graph and imported clip Animator clocks
(`setModel(nullptr)`, then `setModel(&model)`) and resets the clip
selection. This prevents a reused RTS object from resuming a stale
pre-MotionWeave imported-clip time after dynamic asset/controller changes.
When View still provides a valid pose, neither legacy branch updates
its own animation player. Graph evaluation and resulting pose are still
presentation-only; authoritative simulation state is unaffected.

`tests/view/view.cpp` adds a CPU regression scenario with an actual
single-bone Model, a sampled Walk AnimClip, a pure graph using that clip
and a dormant legacy Animator. The scenario checks per-frame View graph
clock and local pose progression, that the dormant Animator clock does not
advance, View-source removal, legacy clock reinitialization and a new
View player after re-entry. This checks the reusable animation contract;
**it does not directly invoke `RtsView::animate`**, so RTS in-game parity
and rendering still need native/visual integration validation.

## Generic transformed bounds for mesh presentation

`engine/view/GeometryBounds.h` exposes
`view::transformAabb(localMin,localMax,worldMatrix)` and a
`view::WorldBounds { min, max, valid }` return value. It transforms
all eight corners, handles rotation, non-uniform/negative scale and
translations, and returns an invalid result for reversed/non-finite
bounds or non-finite output coordinates. Only renderer float math is
involved. Gameplay collision/physics, authoritative ECS and network
serialization do not depend on this presentation helper.

The RTS adapter now consumes this helper when preparing bounds for
animation environment contact meshes, replacing a local eight-corner
AABB loop. Source-specific node/primitive IDs and contact decisions
remain in the game adapter; this shared geometry operation belongs
to View and can be reused by any genre or editor.

Deferred test: `view_geometry_bounds` in
`tests/view/geometry_bounds.cpp`. Native build and execution are
**NOT RUN** for this integration step.

## Genre-free ground ribbons (View Stage 81)

`view::QuadXZObject { start, end, width, rgba, y }` is an ordered
`ScenePacket` for drawing an XZ ground ribbon. Use
`aiverse::View::submitQuadXZ(renderer, packet)` or add it to
`view::ScenePacketBatch` and call `submitPackets`. The renderer's
existing `quadXZ` implementation owns the tessellation and GPU path;
View contains no road, RTS, terrain or gameplay rule. Invalid width
or nonfinite width/height values skip submission.

The legacy RTS road-link overlay now submits the same endpoints, width,
colour and lift through View, with the same immediate overlay layer and
draw order. This is an incremental extraction, **not** retirement of
`game::RtsView`. The mixed packet regression test checks the new
variant and parameter ordering but has **NOT RUN** as of this stage.

## Universal View owns the synchronous layer submission barrier (Stage 82)

The canonical `aiverse::View` now exposes two static submission APIs:

```cpp
View::submitLayers(layerQueue);
View::submitLayers(hostProviders, {seconds, deltaTime, frameRevision});
```

Both consume their work synchronously and preserve the original callback
insertion order, disabled-layer policy, exception semantics and borrowed
captures. A provider-based call collects into a local queue and flushes
before returning. These functions don't schedule work on animation threads
and don't move any gameplay authority into the renderer. Host-specific
terrain, roads, visibility decisions and effects remain owned by their
adapters.

The legacy RTS host delegates both normal and planet-scene layer
submission to `aiverse::View`. The old `RtsView` still constructs
some passes and handles gameplay-derived authoring; it cannot yet be
removed. A deferred `tests/view/view.cpp` regression covers disabled
layers, ordering, single submission and provider collection.

**NOT RUN**: CMake/CTest, native compilation and GPU parity checks
have not been performed on this stage.

## Decoupling editor selection from legacy RtsView (Stage 83)

The native/editor RTS host now uses
`modules/rts/view/RtsSelectionPresentation.h` for visual picking
radii. This is **RTS-specific authoring**, not a generic engine
algorithm: `view_adapter::selectionRadius(const UnitType&)` takes
authored footprint/radius and implements the existing building
footprint and unit radius policy; the overload taking
`const Rules&` validates the type index.

`apps/editor/Editor.cpp::pickEntity` no longer needs a mutable
`RtsView` to calculate picking bounds (four former
`const_cast<RtsView&>` calls removed). The existing
`RtsView::selectionRadius` remains as a compatibility forwarder,
not an independent source of logic. This contributes to retirement
of the legacy renderer without leaking `Rules` or `UnitType`
into `engine/view`.

The larger remaining coupling is explicit:
`apps/client/GameClient.h` still owns a `game::RtsView`;
`apps/client/GameClient.cpp` still calls `draw`, `drawGhost`,
`drawOverlays` and planetary conversion helpers;
`apps/editor/Editor.h` still owns and exposes `RtsView`;
`apps/editor/Editor.cpp` still calls drawing/overlay/preview APIs
and `heightAt`; third-party/specialized game adapters may also use
the legacy view. Full class deletion requires an independent host
scene/presentation adapter plus render, asset and terrain providers,
not simply renaming the old class.

A dedicated `view_rts_selection_presentation` fixture checks unit
and building radii, both Rules lookup overloads and invalid indices.
**NOT RUN** — compiler/CTest and native RTS/editor parity remain
to be executed.

## Standalone RTS ghost and model preview authoring (Stage 84)

`modules/rts/view/RtsPreviewPresentation.h` owns pure host-side
presentation descriptions, independent of the `game::RtsView` class:

- `makeGhostPresentation(type,position,allowed,time)` chooses placement
  position, owner, valid/invalid tint and the existing animated alpha pulse.
- `makeModelPreviewPresentation(type,owner,yaw)` chooses the preview
  identity key and initial facing.
- `previewFrameDt(now,previous)` clamps animation time to the
  historical 0..0.1s window, with a zero-delta first frame.

The legacy `RtsView::drawGhost` and `drawPreview` now consume these
descriptions. Complex model choice, placement-on-planets, LODs, material
overrides and GPU submissions remain in the specialized host renderer,
which already forwards reusable draw commands to `aiverse::View`.
The former `static EntityAnim ghost` has been changed to the
instance-owned `ghostAnim_`, preventing ghost runtime state from
crossing independent editor/client `RtsView` instances.

This is a **bounded architecture migration** and not full removal
of the legacy class. To retire it, extract host-owned model/terrain
resource providers and wire the editor/client to those providers
and the canonical View. The dedicated `view_rts_preview_presentation`
test covers adapter data without constructing RtsView. Native
compile/CTest/visual comparison are **NOT RUN** at this stage.

## RTS preview rendering provider boundary (Stage 85)

The new `modules/rts/view/RtsPreviewRenderProvider.h` is a standalone,
renderer-agnostic host adapter. `RtsPreviewRenderProvider::submitGhost`
requests resource availability, constructs the already-defined
`GhostPresentation` and submits it via the host-supplied drawing callback.
`submitModelPreview` similarly resolves the per-owner preview description,
queries the host-owned animation clock once, clamps the delta and calls the
host's renderer callback. If a type is unavailable, no render or clock
callbacks execute. The provider knows nothing about `RtsView`,
`MapGrid`, `Rules`, ECS or GPU resources.

`game::RtsView::drawGhost` and `drawPreview` now delegate their
orchestration to this provider, supplying their existing model loader,
planet/terrain placement, instance-owned ghost state, animation and
`drawObject` implementation as callbacks. This is the **first renderer
provider seam**, not a completed migration of model rendering: the
specialized legacy renderer still chooses materials, geometry, LODs,
skin poses, attachments and actual GPU submission. The direct editor,
client and portrait call sites still invoke the compatibility methods.

The existing `view_rts_preview_presentation` CTest source now tests
provider callback order, unavailable type short-circuiting and preview
clock forwarding, with no `RtsView` linkage. **NOT RUN**: source and
CMake setup have not been compiled or executed on this stage.

## Static previews without legacy animation ownership (Stage 86)

A new standalone header `modules/rts/view/RtsStaticPreviewModel.h`
accepts a `view_adapter::StaticPreviewModel` description of an
already-resolved model, geometry/material overrides, type presentation
style and model-space correction. Its
`buildStaticPreviewPacket(spec,yaw,packet)` emits a genre-free
`view::Object` with a complete borrowed `render::ModelDraw`.
`submitStaticPreview(renderer,packet)` delegates to
`aiverse::View::submitModel`. It does **not** construct an ECS
animation player, simulate gameplay or depend on `RtsView`.

`RtsView::drawPreview` now uses this independent renderer path
for models satisfying **all** of these requirements: one resolved
model variant; no build/damage stages or distance LOD; no imported
clips, AnimationGraph, bake, pose or procedural bones; no attachments,
weapon poses, additive overlays, FX or leg IK. The host still loads
the model and resolves the authored look. The resulting ModelDraw
preserves the resolved materials, node material map, geometry policy,
KHR variant, model-space correction, world transform, team colour,
tint and static/transparent flags.

Models that need animation, dynamic stages, custom procedural effects,
contact solving or LOD keep the legacy preview path unchanged.
Static-preview submission uses the existing View renderer directly,
so no second MotionWeave World can evaluate the same entity.
The renderer/resource owner must keep borrowed material and model
pointers alive through synchronous submission. This is not complete
`RtsView` retirement; host/editor/client/portrait entrypoints and
resource/terrain providers still need migration.

Per the user's implementation-first instruction, **NO TESTS RUN**
on this stage. Compile, native visual parity and platform validation
remain pending.

## Flat-ground ghost ModelDraw migration (Stage 87)

`view_adapter::buildStaticGhostPacket(spec, position, ground, tint, packet)`
extends the static preview packet path to placement ghosts. It reuses
the borrowed model/material/geometry and transform description, translates
the preview to the sampled flat-ground height, applies the valid/invalid
pulsating ghost tint, disables shadow casting and does not classify ghost
models as immutable/light-baked static scenery.

`RtsView::drawGhost` now submits eligible flat-ground rigid objects
directly via `aiverse::View::submitModel`. Its eligibility deliberately
requires one model choice without stage/LOD overrides, imported clips,
AnimationGraph, pose/baked skinning, procedural animation/IK,
attachments, weapon poses, additive overlays or effects. Planetary
placement, complex rendering and noneligible models still use the
existing compatibility draw path, which continues to own their model
lifecycle and special model placement.

This is not the deletion of legacy `RtsView`; its API and host
callers remain intact. **NOT RUN:** tests, compilation and visual
parity checks were intentionally deferred by the user.

## Shared RTS preview resource resolution (Stage 88)

`modules/rts/view/RtsPreviewResourceProvider.h` supplies
`view_adapter::resolveStaticPreviewModel(look,type,playerColor,out)`.
The host passes already-loaded appearance and type descriptions; the
provider applies the **same** eligibility test for ghost and portrait
previews, then builds one `StaticPreviewModel` with borrowed model,
materials, node-material bindings, geometry policy, variant, correction,
type transform settings, tint, and squared linear team color.

Both `RtsView::drawGhost` and `drawPreview` now call this single
resolver before their respective `buildStaticGhostPacket` and
`buildStaticPreviewPacket` submission. Ineligible models continue
unchanged through `RtsPreviewRenderProvider` and the legacy
animation/material/planet-specific draw path. No new model copies or
animation players are introduced in the direct path.

Resource residency/loading and lifetime are **still host-owned**;
the resolver is a renderer-independent borrowed-data adapter, not a
new asset cache. It has no dependency on `RtsView` or engine-side
genre enums and cannot mutate gameplay ECS state. The old class
and client/editor/portrait call sites are not yet retired.

**NOT RUN**: native compiler, CMake/CTest, CI, graphics parity and
sanitizer checks remain deferred.

## External preview model-resource ownership (Stage 89)

`modules/rts/view/RtsModelResourceProvider.h` introduces a host-facing
`view_adapter::ModelResourceProvider` contract and the
`submitResolvedPreview(resources,renderer,type,owner,yaw)` helper.
The provider resolves model residency and appearance into the existing
borrowed `StaticPreviewModel`; the helper produces one generic
`view::Object` and submits it through `aiverse::View` without an
animation update. It returns false for unsupported assets, allowing
the consumer to invoke its existing animated/complex fallback.

The legacy `RtsView` implements this resource contract temporarily.
It still owns models, GPU resources and per-type appearance data, but
`Portrait::render` now consumes the contract directly and submits
eligible static models using its own View-backed renderer instead of
calling `RtsView::drawPreview`. Animated portraits, LOD, contact IK,
attachments and specialized effects still call the old method.
`RtsView::drawPreview` uses the same resolver to avoid duplication.

This is **resource access separation**, not independent upload/cache
ownership and not the deletion of `RtsView`. Model and material
pointers are borrowed only until synchronous submission ends.
Any future independent editor or project model provider can implement
this interface without inheriting from `RtsView`.

**NOT RUN**: user deferred native compilation, CTest, rendering
parity and CI until feature work is complete.

## Per-host RTS model/texture ownership (Stage 90)

`modules/rts/view/RtsModelResourceCache.h` is the new per-host
`view_adapter::RtsModelResourceCache`. It owns the canonical
`std::map<std::string,std::unique_ptr<render::Model>>` model assets
(including procedural `shape:` models) and the shared
`render::ModelTextureCache`. Its surface includes
`models()`, `textures()`, `stats()`, `modelFiles()`,
`trimUnused(device,used,onRelease)` and
`shutdown(device)`. GPU destruction/texture trimming happen on the
render thread after outstanding asynchronous preparation has joined.
An optional host callback invalidates graph/animation entries before
a model is destroyed.

`RtsLook.cpp` model loading, asynchronous prepared-result insertion,
residency trimming, and model listing now access this one cache.
`RtsShapes.cpp` inserts procedural fallback shapes into the same
owner. `RtsView.cpp` delegates complete model/texture shutdown
instead of independently iterating raw model pointers. Existing
`RtsView::textureCache()`, `assetResidencyStats()` and
`modelFiles()` remain source compatible and forward to the owner.
A read-only `RtsView::modelResources()` accessor exposes this
per-host resource owner to native presentation adapters.

The resource cache itself does **not** depend on RTS Rules, View
simulation, FPS, AnimationGraph scheduling, or genre-specific game
commands; it stores `render::Model` and texture assets only.
The legacy `RtsView` still *contains* its resource-owner instance
and still selects/loading model variants. This stage is ownership
extraction, not fully independent lifecycle or complete elimination
of RtsView.

**NOT RUN:** per user instruction, no CMake/configure, native compile,
CTest, sanitizers, Metal graphics, CI or visual comparison was executed.
Before shipping, verify resource release ordering, shared-texture
hot reload, streaming cancellation, and asset residency under editor
scene switching.

## Independent model preparation lifecycle (Stage 91)

`modules/rts/view/RtsModelPreparationService.h` provides a
per-resource-owner asynchronous CPU preparation queue. It owns the
current source type/variant request, ordered source/key list, cursor,
displayed in-progress resource name, and at most one `std::future`
for a CPU-prepared `render::Model` with warnings. The service
exposes `begin`, `appendPath`, `launch`, `ready`, `take`,
`advance`, `reset`, `progress` and resource state queries.
`begin`/reset join any active task before discarding its state.
The caller still supplies immutable model/reader/texture-cache
inputs to the CPU task and publishes GPU uploads on the render thread.

The per-host `RtsModelResourceCache` **owns** this scheduler on
non-Emscripten builds and resets it before freeing models and textures.
`RtsView` no longer carries independent worker futures, type/cursor
or path state. `RtsLook.cpp` sends requests and polls completion
through `resources_.preparation()`; it still determines RTS model
choices, skinning policy, variant order, warning destinations and
render-thread uploads. Clearing loaded types or replacing hot-reloaded
models continues to be blocked when a preparation request is active.

This separates resource scheduling and lifetime from legacy rendering,
but it is **not yet a fully independent RTS asset-loading service**:
the host still owns source discovery and the model-file loading policy,
and it still stores rules-driven appearance data. New native hosts can
own `RtsModelResourceCache` independently and supply equivalent CPU
work/publish callbacks without instantiating `RtsView`.

The browser/Emscripten path retains its original synchronous/no-worker
behaviour. **NOT RUN:** no CMake, compiler, CTest, sanitizer, Metal
visuals or CI on this feature stage, by user request.

## Reusable model streaming operations (Stage 92)

`modules/rts/view/RtsModelStreaming.h` provides two host-agnostic
model-loading building blocks: `discoverModelPaths(authored,geometry,
alreadyCached,append)` traverses an authored JSON subtree in existing
order (including model variants, LOD and attachments), and
`prepareModelCpu(path,reader,textures,{geometry,animationBake})`
performs model decoding, optional cluster preparation, optional
animation-bake preparation and CPU upload staging. The latter returns
a `RtsModelPreparationService::PreparedModel` with warnings; it does
**not** issue GPU commands.

`RtsModelResourceCache::publishPrepared(device,key,prepared)` runs
synchronously on the render thread, skips replacing an asset already
published by a synchronous loader, and uploads/inserts a prepared model
(including the existing null-model failure cache semantics). The cache
still owns the model/texture lifecycle, while
`RtsModelPreparationService` owns the future and ordered queue.

`RtsLook.cpp::prepareType` now composes these three operations.
The RTS host retains policy decisions: which authored variants to
request, geometry/skinning flags, warning routing, and when to poll
or publish a result. Native hosts may reuse these functions and own
a `RtsModelResourceCache` without constructing `RtsView`.
The service remains serial (one in-flight CPU job per owner);
browser/no-worker behaviour is unchanged.

This moves the streaming **operations** out of the legacy class, but
does not yet replace RtsView's rules-driven type choice and residency
coordination. Material variant configuration and model hot-reload
invalidations still require the owning host. No animation ECS,
networking, save data, or renderer packet contract changed.

**NOT RUN**: native build, CTest, sanitizer, GPU visual parity, CI,
and cross-platform verification remain deferred per user instruction.

## Native editor model demand scheduler (Stage 93)

`modules/rts/view/RtsModelDemandScheduler.h` adds a small host-owned
policy service for model demand. It tracks per-type last-use times,
batches and de-duplicates demand requests, upgrades each request to
the highest priority (`Background`, `Visible`, `Interactive`),
drains in descending priority and FIFO within each priority,
and exposes periodic reclamation and recent-use decisions.
`resize()` resets demand on rules/hot-reload changes and advances
an independent revision, invalidating queued requests.

`apps/editor/Editor` now owns this scheduler instead of a separate
last-use vector and trim timestamp. It registers interactive requests,
drains them immediately so Inspector and material previews remain
responsive, and uses the scheduler's retained-time policy for its
existing 5-second sweep and 15-second grace. The original map,
selection, placement and overlay-preview keep rules still apply.
Both lazy and eager modes consume the queue; eager mode does not
reload already-resident models. Rules reload resets scheduler state.

`RtsModelResourceCache::generation()` additionally exposes a
monotonic counter updated on `publishPrepared`, `trimUnused`
and `shutdown`. **Important limitation:** the existing legacy
`models()` mutable map access is still supported and does not
increment this counter; callers must not interpret it as a complete
hot-reload epoch until all insertion paths are routed through the
owner API.

This feature establishes an externally owned scheduling/policy
component and integrates it with an actual editor host. It does not
yet move all rules-driven asset selection/hot reload from `RtsView`
or change MotionWeave. **NOT RUN**: CMake/native compiler, CTest,
Metal screenshots, sanitizers and CI remain deferred per user request.

## Tracked model cache writes and budgeted editor prefetch (Stage 94)

`RtsModelResourceCache` now exposes read-only `models()`, plus
`contains`, `find`, `insertIfAbsent`, and `replace(device,key,
replacement,onRelease)`. All known RTS model-map writes have moved
through this resource-owner API: synchronous imports preserve null
failure-cache entries, worker results use `publishPrepared`, and
generated shapes use tracked replacement which releases prior GPU
buffers and invalidates their host animation-cache entries.
`generation()` now advances on those writes, unused-model eviction,
and shutdown. This covers every currently identified mutation in the
RTS loaders, but callers must still rebuild/invalidate borrowed raw
`Model*` pointers after replacement, trim, or full reload.

The editor's `RtsModelDemandScheduler` adds `drainBudget(budgetMs,
maxCount,load)`, prioritizing interactive/visible requests and
preserving FIFO within equal priority. The editor continues to service
interactive selection/Inspector loads synchronously. Its residency
sweep queues missing *retained* map/type models for background
prefetch, and each non-playing non-FPS editor frame consumes at most
two requests with a nominal 1 ms wall-clock budget. Because
`loadType` can be synchronous, one load may exceed this soft budget.
Rules reload resets demand and the existing RtsView shutdown releases
all cache resources, advancing their generation; targeted per-file
hot reload still needs a safe external invalidation API.

Genre-specific keep rules (map references, current selection,
placement/overlay preview, and recent use) remain in the editor.
The generic engine View and MotionWeave are unchanged. RtsView is
still a compatibility host and owns the cache instance.

**NOT RUN**: native compilation, CMake, CTest, sanitizers, Metal
screenshots, editor/RTS/FPS smoke runs and CI were deferred at the
user's explicit request.

## Editor presentation-only asset refresh (Stage 95)

The model watcher in `apps/editor/windows/ModelWindow.cpp` now batches changed
**resident model files** into an editor-local pending set. It retains changes while
either in-editor game play or client play is active and flushes them once both
modes have ended. It resets timestamps and queued paths on a project switch,
avoiding reload requests leaking between projects.

`Editor::reloadVisualAssets()` re-creates the compatibility view on the editor
render thread, which joins outstanding CPU model-preparation workers before
destroying GPU model/texture resources. It invalidates queued model demands,
preserves last-used timestamps, requeues retained authored/selected types and
increments preview revision so borrowed scene data is rebuilt. Unlike
`reloadRules()`, this path does **not** reparse project rules, clear the
simulation, reset the editor's document or restart `reloadFx()`. Existing
synchronous interactive loads and lower-priority opportunistic prefetch remain.

This is a **whole-visual-view refresh**, not selective reload of changed models
or animation graphs. The watcher currently sees only model paths reported by
`RtsView::modelFiles()` (resident assets), not every uninstantiated type,
texture dependency or external graph JSON. The model cache's tracked
`generation()` is not yet a per-source dependency revision. A later stage
must index source paths to cache keys, detach all referencing TypeLooks and
presentation/animation graph instances, and retire changed GPU resources only
after that barrier. FPS visual resource reload and native project host adapters
remain separate work. Native build, CTest, sanitizers and Metal/editor visual
verification are **NOT RUN**, per request to implement features first.

## Selective model-source and animation presentation reload (Stage 96)

Model/animation hot reload no longer requires a full `RtsView` restart for a
changed **resident model source**. The editor watcher invokes
`Editor::reloadVisualModelSources(paths)`, which invokes
`RtsView::reloadModelSources(paths)` on the presentation/render thread
outside play mode. `Editor::reloadVisualAssets()` remains available as the
legacy full-view fallback for non-model asset categories.

The independently owned `view_adapter::RtsModelResourceCache` now supports:
- `sourceForKey(key)`: maps both `model.glb` and `model.glb#geometry`
  cache entries to the original source path.
- `affectedBySource(path)`: enumerates affected keys and borrowed model
  pointers; importantly, failed-load/null entries are also represented.
- `invalidateSources(device,changed,onDetach)`: joins any pending native
  CPU preparation, invokes the host detachment callback **before** any
  cached GPU model destruction, evicts only affected keys, advances cache
  generation and runs texture trimming. The callback must clear all
  external borrowed model/animation references and is invoked on the render
  thread between submissions; a throwing callback aborts before eviction.
- `modelFiles()`: reports deduplicated normalized source paths even for
  geometry-policy variants and failed loads, so a fixed missing model can
  be reloaded without reopening the editor.

The RTS host's detachment phase identifies loaded appearances through
current `ModelChoice` pointers (including attachments, stage choices and
LOD choices) and through recursive authored model references (covering
failed loads, late variants and nested objects). Streaming
`coarseModel/detailModel` and requested runtime models are checked too.
Affected `TypeLook` and `EntityAnim` records and per-model compiled graph
caches are invalidated, then only previously loaded affected types are
reconstructed. `MotionWeaveSourceBridge::forget` and
`aiverse::view::View::forgetSources` remove only affected presentation ECS
entities; unrelated animation clocks are not deliberately restarted.
The ephemeral ghost animator is reset conservatively. There is no second
animation tick or owner.

**Scope and constraints:** the editor watcher still enumerates only
resident cache sources, not all unloaded authored assets. File changes to
external graph definitions, materials, stand-alone textures and independent
FPS host caches are not routed into this targeted model path. They require
explicit dependency indexing and safe texture/graph invalidation next.
GPU submission completion, shared texture retention, active portrait
render targets and exact visual parity require native/graphics validation
before this is production-safe. `RtsView` remains a compatibility adapter.

**NOT RUN** by request: no local native compiler, CMake/CTest, Metal/RTS/FPS/
editor runtime, ASan/TSan, benchmarks or CI. The code was committed through
the GitHub API; source review is not a substitute for compilation.

## Source dependency index and revisioned external visuals (Stage 97)

`game::view_adapter::ModelSourceDependencyIndex` (new
`modules/rts/view/RtsModelSourceDependencies.h`) provides a reusable,
host-owned index of source path -> dependency categories and type IDs.
`rebuild(types,materialLibrary)` uses authored view descriptions, not
loaded models: nested variants, LOD, stages, attachments, streaming
coarse/detail model paths, referenced library materials, direct texture
overrides and optional external `animationGraph` paths are included
even for missing and not-yet-streamed sources. A host can add
`addResidentModel` and `addResidentTexture` entries from already
loaded render Models to catch imported glTF/etc external texture files.
`resolve(changedFiles)` returns impacted types, model sources and
category flags; paths are lexical-normalized. No workers, GPU, or game
simulation state are owned by the index.

`ModelWindow` rebuilds this index during its existing once-per-second
asset scan, safely excludes absolute/parent-traversal watch paths,
remembers both present and absent file timestamps and notices
delete/recreate transitions. Watch state is reset when changing projects.
Requests remain queued while in-game or client play is active.
Unchanged imported-model conversion behaviour is retained.

For **model changes**, Stage 96's selective model-source transaction
is used, with all authored model variants of affected types rebuilt.
For a **texture change**, the editor first increments that texture
source's revision through `render::ModelTextureCache::invalidateSource`,
then reloads the affected model sources. The decoded CPU entry is
discarded; the shared GPU cache uses a revision-suffixed key for new
uploads, leaving old texture leases with existing borrowed models.
When model records release, the normal cache trim collects old GPU
entries whose leases are no longer retained. No borrowed GPU texture
is forcibly destroyed by invalidation. If a texture affects an object
without any resolvable model source (e.g., a built-in shape), the
editor chooses the full-view refresh fallback.

`view.animationGraph` now also accepts a JSON asset **path string**
in addition to the existing inline graph object; the renderer-host
file reader loads and validates the external definition. The editor
watches external graph paths and falls back to the existing full
presentation refresh to avoid crossing model/graph lifetimes.
`materials.json` edits also use the whole-view fallback until
library material revision and ownership are independently managed.
`AnimationHotReloadRegistry::apply` now requests player restart and
clears stale montage/notify state on a changed graph, while
`forgetModel(model)` removes last-good compiled graphs before a host
destroys their `Model`. This generic registry API is **not** yet used
by the transitional RTS host, which has its own graph cache.

No JSON schema change is required for existing inline graphs, models or
type references. This stage adds optional file-backed graph syntax,
new C++ helpers, and an internal GPU texture-cache revision suffix.
There are no authoritative ECS, network/save/checksum or gameplay
changes and no duplicate animation evaluation.

**NOT RUN:** native build, CMake, CTest, Metal/editor/FPS/RTS screenshots,
sanitizers, CI, device/GPU fence checks and performance benchmarks.
Test sources `tests/animation/model_dependencies.cpp` and additions
to `tests/animation/hot_reload.cpp` were authored but not executed.

**Important limits:** the source index does not dynamically parse
nested external graph includes, imported model-specific sidecars that
are not exposed as models/images, runtime-generated external texture
paths that never enter the cache, or arbitrary dependencies produced by
custom loaders. The watcher currently lives in ModelWindow and is
editor-only. Targeted reload still depends on RtsView for appearance
rebuild and must run between GPU submissions. Model-derived external
materials/texture lease lifetimes and render synchronization require
real platform verification before production deployment.

## Window-independent visual reload and staged appearance transactions (Stage 98)

The editor now owns `VisualAssetDependencyService`
(`apps/editor/core/VisualAssetDependencyService.h`) directly as a member of
`Editor`. `Editor::pollVisualAssets(dt)` runs once during the normal
`Editor::frame` lifecycle after document changes and **before** any viewport
render submission. It is not contingent on `ModelWindow::open` or on any
individual window callback. `ModelWindow` is now strictly a UI and
`model_info` presenter; its former watcher state and process launcher have
been removed.

The service keeps the active project, 1-second scan clock, optional file
timestamps (including missing sources), per-file source conversion attempts,
pending changed paths and a `ModelSourceDependencyIndex`. It rebuilds its
dependency snapshot using authored rules/material library and the resident
model cache at each scan. It never mutates renderer resources. Deferred
changes survive editor or client play until an editor-frame safe boundary;
`reset` runs on rules/project changes and removes old timestamps/requests.
The old `modelconv` invocation now belongs to the Editor host as
`Editor::convertVisualModelSource`, with source modification-based deduping.
External graph file paths nested in authored model choices are now watched
as well as top-level graph references.

For graph-only or `materials.json`-only modifications,
`RtsView::reloadAppearanceTypes(types,reloadMaterialLibrary,&count)`
implements a staged appearance update. The host joins any pending CPU
model-preparation task, parses the new material library transactionally if
requested, builds candidate `TypeLook` resources for all affected **loaded**
types while retaining old looks, and rejects the batch if a previously
present model/graph would disappear or candidate building throws. A rejected
edit keeps existing animation playback and prior material library contents.
After successful staging, affected MotionWeave presentation source IDs are
unlinked via `View::forgetSources`, only their `EntityAnim` and streaming
choice state is cleared, and only affected `TypeLook` records are swapped.
Unrelated models and animation players keep their old cache residency,
player entities and clocks. Previously unloaded types naturally use the
latest file/library when loaded later.

Mixed source changes involving a graph/material together with model/texture,
or texture changes with no resolvable model source, retain the earlier
whole-view compatibility fallback. Model-only and recognized texture-only
changes still use Stage 96/97 selective model cache invalidation and
revisioned texture leases. This is deliberately not an attempt to mix GPU
model destruction with appearance graph swapping in one transaction.

**Public surfaces:** `VisualAssetDependencyService::due/scan/impact/
changedSources/acknowledge/reset` are editor-only; the generic
`ModelSourceDependencyIndex` and MotionWeave source bridge retain the same
engine boundaries. `RtsView::reloadAppearanceTypes` is an additive
compatibility-adapter API. No simulation ECS, save/checksum, networking,
render packet or authored JSON format changes. The `RtsView` compatibility
owner is NOT removed.

**GPU lifetime caveat:** this stage does NOT introduce a GPU completion
fence. It provides the CPU/presentation-thread staging boundary, but the
backend's asynchronous use of GPU buffer and texture allocations still
requires platform-specific lifetime validation, and shared material images
may remain in the resident model until that model is evicted. Stage 98's
selective appearance updates avoid destroying resident GPU model buffers,
but full-view/model-source fallback paths retain their previous
destruction policy.

**NOT RUN**, per user instruction: no local clone/native CMake compiler,
CTest, sanitizers, Metal/editor/FPS/RTS screenshots or GPU resource fence
verification. Deferred test source
`tests/animation/editor_visual_asset_service.cpp` exercises service
ownership independent of a window, initial timestamp seeding,
external graph detection, deletion/recovery, pending retention and project
switch reset. `CMakeLists.txt` registers
`animation_editor_visual_asset_watcher` for future native execution.

## Stage 99 — portable GPU resource retirement and universal model owner

**Renderer API:** `gfx::Device::waitForResourceIdle()` is a new
synchronous, explicit owner-thread barrier. It returns `true` only
when all **previously submitted** GPU work has completed; default
implementations return `false` rather than guessing that a given
`frameSerial()` represents completion. Call outside render/compute
encoders, **before** issuing GPU work this frame that could reference
retired resources. A queue-ordered Metal command buffer is committed
and waited for; Vulkan uses `vkDeviceWaitIdle` followed by completed
garbage collection; OpenGL/GLES uses `glFinish` with error inspection.
These are intentionally blocking correctness barriers, not a low-latency
async fence/timeline implementation.

**New genre-independent ownership classes:**

- `<engine/render/ModelPreparationService.h>` —
  `aiverse::render::ModelPreparationService`: host-owned CPU worker,
  ordered path requests and progress with explicit render-thread GPU
  publication policy.
- `<engine/render/ModelResourceCache.h>` —
  `aiverse::render::ModelResourceCache`: per-host owning source map,
  texture cache, preparation worker, generation, model variants and
  resource invalidation. Native FPS, RTS, editor and standalone project
  code can instantiate this without importing RTS modules.
- `<engine/render/ResourceRetirementQueue.h>` —
  generic `render::ResourceRetirementQueue<Resource>` queues move-only
  resources and invokes destruction only after an explicit fence
  callback returns success.

Compatibility headers in `modules/rts/view` now alias
`RtsModelResourceCache` and `RtsModelPreparationService` to those
generic render classes, preserving old method signatures and
`view_adapter` names. No conversion of game-authoritative state
is involved. No special renderer view object or second animation
clock is created.

**Release lifecycle:** `ModelResourceCache::replace`,
`invalidateSources` and `trimUnused` first detach host
`TypeLook`/`EntityAnim`/MotionWeave and then transfer removed
`unique_ptr<Model>` instances to `retired_`, retaining their GPU
buffers and shared texture leases. `pendingRetirements()` reports
the deferred queue. `collectRetired(device)` calls the backend
completion barrier once for a nonempty queue, releases old models
only after confirmation, then trims unleased texture-cache entries.
If a backend cannot prove completion, the full queue remains intact.
`shutdown` joins pending CPU jobs, waits for previously submitted GPU
work, then releases resident and retired models and finally textures.
It **fails closed with an exception** if the GPU barrier fails,
rather than destroying potentially active resources. It remains
the host's responsibility to call shutdown at a safe frame boundary
and not while a current frame has unsubmitted references.

**Real owner integrations:**

- `Editor::frame` collects completed retirements before viewport
  submissions; live `GameClient::frame` uses the same barrier before
  rendering RTS scene sources. There is no per-object wait or per-draw
  GPU stall.
- `game::Portrait` uses its *own*
  `render::ModelResourceCache previewModels_` to own the generated
  sphere and box of material previews, including their GPU lifetime,
  instead of raw `unique_ptr<Model> shapes_[2]`. This is a concrete
  independent editor/portrait resource owner. Its offscreen render
  target, material authoring and complex/animated preview fallback
  remain as before.
- Resource cache API is usable from native project C++ by including
  `engine/render/ModelResourceCache.h` and linking the existing
  `aiverse_models`/engine rendering target.

**Deferred tests:** source fixtures
`tests/render/resource_retirement.cpp` and
`tests/render/model_resource_provider_contract.cpp` are registered
as `resource_retirement_queue` and
`model_resource_owner_contract` CTest entries. The former simulates
a denied then granted completion barrier and verifies FIFO cleanup;
the latter verifies the generic/compatibility aliases and source
cache APIs without uploading GPU resources. **NOT RUN:** no local
compiler, CMake, CTest, screenshots, sanitizer, CI, backend GPU
execution or memory-life profiling.

**Limits and risks:** the barriers are conservative *blocking* waits
and can pause a frame when resource retirement is requested. An
asynchronous completion-token/timeline queue has not been implemented.
`waitForResourceIdle` does not flush or synchronize unsubmitted
GPU commands in the **current** frame, so hosts MUST only call it
before those commands can reference any retired object. Other older
engine renderers, FX targets and raw device clients still destroy
GPU handles directly and have not been converted to the universal
owner. Full legacy `game::RtsView` removal remains incomplete.
Native compilation and Metal/Vulkan/GL correctness are UNVERIFIED;
do not merge or claim runtime parity before the deferred checks.

## Stage 100 — nonblocking GPU retirement timeline and native model provider

`gfx::Device` now offers a token-based asynchronous GPU completion API:

- `beginResourceRetirement()`: return a nonzero ordered token after all
  **previously submitted** GPU work on the owning device/queue.
- `resourceRetirementComplete(token)`: nonblocking owner-thread polling;
  only `true` authorizes model/texture destruction.
- `endResourceRetirement(token)`: release a completed token's backend
  fence/callback state, never a still-running marker.
- `waitForResourceIdle()`: existing blocking fallback and final shutdown
  barrier; still fail closed if the device cannot guarantee completion.

Metal commits an empty queue-ordered command buffer and uses its completion
handler's atomic status; Vulkan queues a zero-command-buffer fence submission
and polls `vkGetFenceStatus`; GL/GLES/WebGL2 enqueues `glFenceSync`, flushes
the context and polls `glClientWaitSync(..., 0, 0)`. Windows GL's loader
now resolves the sync API. The implementation is owner-thread-only except
for the Metal completion handler. Frame sequence numbers and fixed delays
are NOT treated as GPU completion.

`render::AsyncResourceRetirementQueue<Resource>` keeps independent batches
for each nonblocking token. New model evictions after a token was issued
are never freed on that older token. At the next frame boundary
`ModelResourceCache::collectRetired(device)` releases any completed batches
and schedules newly queued models without stalling the GPU; it only uses
the blocking `waitForResourceIdle` fallback when token creation fails or
is unsupported. Shutdown still requires an explicit successful full-device
barrier and drains all remaining batches before destroying the texture
cache. Existing `ResourceRetirementQueue` remains for synchronous users
and its original tests.

### Reusable native model provider

New `engine/render/ModelAssetProvider.h` implements a per-native-host
source loader, uploading and caching ordinary authored models through
`render::ModelResourceCache`. It accepts a project-supplied
`render::FileReader`, returns a borrowed `Model*`, exposes failed-load
warnings, selective source invalidation, async retirement collection and
explicit shutdown. It knows no RTS, FPS, editor or simulation entities.
Native projects may pass its borrowed `Model*` to the existing generic
`aiverse::View::submitModel` or construct their own `view::Object`.

`game::Portrait::renderStaticModel(ModelAssetProvider&,source,yaw)` is a
real static model consumer: its renderer and canonical View submit the
provider's model without constructing `game::RtsView`, creating another
MotionWeave graph evaluator or assuming the resource provider owns game
rules. Complex/animated RTS portraits keep the existing compatibility
fallback until further migration.

### Verification / limitations

Source regression `tests/render/async_resource_retirement.cpp` checks
multiple retirement batches, out-of-order requests, missing token fallback,
failure to prove GPU idle, shutdown drain and exact destruction order.
`tests/render/model_asset_provider_api.cpp` is a native C++ consumer API
fixture without a real GPU. Both are registered in CTest (names
`async_resource_retirement_queue` and `model_asset_provider_api`).
**NOT RUN:** compilation, CTest, Metal/Vulkan/GL/WebGL2 native GPU runs,
sanitizers, graphics screenshots, frame-time profiling, CI and memory
retention checks remain explicitly deferred.

Token creation is legal only between frames or before current-frame GPU
commands may borrow retired resources. A token does not account for
**unsubmitted** commands recorded in the current frame. The fallback
`waitForResourceIdle` may still stall when a backend cannot create a
token. Fences with device loss can remain pending; the host must handle
GPU loss safely rather than assume a timeout proves completion. The
backend-specific implementations and lifetime ordering are not yet
validated on real hardware. Scene-level (RTS View) legacy retirement
and custom FX/terrain caches still need their own migration; don't merge
or claim parity before native verification.

## Stage 101 — bounded retirement pressure, async CPU imports and editor raw-model preview

### Resource pressure / diagnostics

`render::AsyncResourceRetirementQueue<Resource>` now exposes
`stats()` and `saturated(limit)`. Statistics include queued resources
(in submitted and unsubmitted batches), active fence batches, unsubmitted
resources, high-watermark, number of successfully released resources and
batches, repeated pending polls, async-token creation fallbacks and failed
synchronous-idle attempts. A **pending** GPU token is normal; it must not
be reported as a device error. A zero token means unsupported *or*
failed asynchronous marker issuance, not proof of GPU loss.

`render::ModelResourceCache` offers
`retirementStats()`, `setRetirementSoftLimit(count)`,
`retirementSoftLimit()` and `publicationBackpressured()`.
The default soft limit is 128 **retired model objects**; zero disables
the limit. This is an advisory allocation throttle for new generic
source-provider loads, NOT a hard cap on in-flight allocations and not a
VRAM byte quota. Existing resident models keep working; worker results
already prepared by the legacy RTS service are not discarded simply
because the queue reaches its limit. No timeout or backpressure signal
ever allows a model to be destroyed before its GPU fence completes.

`RtsView::assetResidencyStats` forwards these counters and the Stats
window displays waiting models, fence batches, queue peak, throttle
state and fallbacks. The actual GPU texture bytes, shared geometry-pool
allocations and other render targets remain outside this statistic.

### Generic asynchronous native-source loader

New `engine/render/AsyncModelCpuQueue.h` contains a reusable,
single-worker native CPU import queue. `request(path,reader,textures)`
deduplicates active/queued paths, limits the waiting queue (default 16),
and executes `loadModel` plus `prepareModelUpload` on a background
worker. No GPU Device/Renderer/ModelDraw functions are invoked there.
`pollReady(Prepared&)` is nonblocking on the owner thread;
`invalidate(path)` advances the source revision and removes queued
requests, so an already running stale CPU result is rejected instead
of being published. `shutdown()` joins the worker before the project
reader and texture-cache lifetime ends. The application-supplied
`FileReader` MUST be thread-safe.

`render::ModelAssetProvider` integrates that CPU worker with
`requestAsync(path)`, `cpuStats()` and
`publishReady(device,maxCount)` (native builds). A project can use
the following frame-boundary pattern:

```cpp
render::ModelAssetProvider provider(projectReader);
// On demand, without blocking the render thread for CPU import:
provider.requestAsync("models/prop.glb");
// During a later render-thread frame, before scene submission:
provider.collectRetired(device);
provider.publishReady(device, 1);
if (const auto* model = provider.find("models/prop.glb")) {
    render::ModelDraw draw;
    draw.model = model;
    aiverse::View::submitModel(renderer, draw);
}
// During project shutdown, on the device owner thread:
provider.shutdown(device);
```

The generic provider pauses admission/publication of NEW models if
retirement pressure is at its soft limit, but does not invalidate
finished CPU work prematurely. Its old synchronous `load` path is
still available. Emscripten remains on the synchronous path; no CPU
threads are required for browser projects.

### Actual editor consumer

`apps/editor/windows/ModelWindow.cpp` adds an explicit alternative
*“Исходная 3D-модель (без overrides)”* for imported static unskinned
models. Only when this inspection mode is selected, the window owns a
separate `ModelAssetProvider`, asynchronously imports the raw model,
and calls `Portrait::renderStaticModel(provider,path,...)` through the
canonical `aiverse::View`. The imported original deliberately omits
type-level colour, material overrides, graph/procedural animation,
attachments and authored transform correction. This is **not** a
replacement for the normal type-appearance preview; the latter remains
the default and retains the legacy path for fidelity. The independent
provider joins/releases its resources on project/cache revision changes
and editor window destruction.

### Validation and limits

New deferred CPU fixtures:
`tests/render/retirement_pressure.cpp` (`render_retirement_pressure`)
and `tests/render/async_model_cpu_queue.cpp`
(`render_async_model_cpu_queue`, native-only with Threads) are
registered in CMake. The first checks resource-pressure telemetry,
high-watermark, per-batch completion and fail-closed fallback; the
second checks worker request coalescing, bounded waiting queue,
invalidated-result rejection and safe shutdown. All are **NOT RUN**,
along with native builds, CTest, GPU backend tests, sanitizers,
screenshots and FPS/RTS/editor play parity, by the existing
features-first test deferral.

**Remaining issues:** the soft limit does not cap memory from all
legacy RTS model-publication paths, FX, terrain, texture bytes or shared
geometry pages; worker preparation is one-at-a-time, not a parallel
priority job system; the editor source preview is opt-in and differs
visually from a fully authored type by design. Backend device-loss
recovery and GPU-fence error states still require real hardware
validation. The generic resource owner is suitable for native projects,
but the full legacy `game::RtsView` scene adapter is not retired.

## Stage 102 — estimated GPU retirement footprint and prioritized native asset publication

### Source-derived GPU footprint (not driver VRAM)

`engine/render/ModelResidentEstimate.h` offers
`render::estimateModelResident(const render::Model&)`. It approximates
dedicated model vertex/index buffers using model or authored primitive
counts (even after CPU vertex/index arrays are freed), counts base-level
bytes of **owned, non-borrowed** model images, and estimates RGBA32F
baked-pose matrices from joint/row counts. It intentionally excludes
shared geometry-pool pages, borrowed shared textures, unknown morph
texture height, mip/driver alignment and other backend allocations.
The estimate is **not** exact VRAM, and differing loader batching or
compressed formats can make it over- or under-estimate usage.

`AsyncResourceRetirementQueue::estimatedPendingBytes(estimate)` walks
both unsubmitted resources and existing GPU-token batches; totals are
saturated rather than overflowing. The generic
`ModelResourceCache::estimatedRetiredGpuBytes()` sums the known-model
estimates and exposes
`setEstimatedRetirementByteLimit(bytes)` and
`estimatedRetirementByteLimit()`. This is an optional, default-disabled
(0) source-derived **admission** limit for NEW
`ModelAssetProvider` loads, combined with the previous retired-object
soft limit. No estimate, backlog or device-error status ever authorizes
early GPU destruction. The editor Statistics panel displays estimated
retired model MiB with an explicit shared-pool exclusion.

### Priority and render-thread upload pacing

`AsyncModelCpuQueue::request` accepts an optional
`ModelLoadPriority::Background/Visible/Interactive`; jobs already
queued can be upgraded (not duplicated), and work at equal priority
retains insertion order. One CPU worker continues to run at a time,
with its original revision rejection, bounded queue and worker-thread
reader contract. Priority applies to **pending** jobs; an already
executing CPU decode is not preempted.

`ModelAssetProvider::requestAsync(source,priority)` forwards priority.
`publishReady(device,maxCount,budgetMs)` adds an advisory time limit:
it checks elapsed wall time before starting each subsequent GPU upload
and never drops a ready model. The first upload may exceed budgetMs,
so this is not a hard GPU upload time guarantee. Count remains a hard
per-call cap. GPU uploads and publication continue to run only on the
owning render thread, never in CPU workers.

The Editor Model window's optional raw static preview now requests
`Interactive` priority and publishes at most one model with a nominal
1 ms budget per frame. Its independent provider uses an advisory
64 MiB *estimated-retirement* limit. This does not change the default
type/character portrait path, nor its authored material, graph or
placement semantics.

### Second real native View consumer

The standalone `aiverse_modelview` tool has an opt-in
`--provider --no-merge` path for ordinary raw static assets with no
requested animation clip and no baked-skinning mode. It loads those
models through its own `render::ModelAssetProvider` instead of the
legacy raw ownership map. Both the opt-in and legacy paths now submit
their existing `ModelDraw` (world transform, imported materials,
variants, team colour and precomputed animation pose) through canonical
`aiverse::View::submitModel` without a second graph tick.
The option intentionally excludes merge and animated-model paths,
which retain their previous import/motion logic. It is a comparable
static source inspection path, NOT a blanket switch of all games to a
new renderer or a claim that `RtsView` is retired.

### Deferred verification

New tests registered through ordinary CMake/CTest targets:
`tests/render/model_estimate.cpp` (`render_model_estimate`) checks
geometry/owned-image/baked-pose estimates and shared-texture exclusion;
`tests/render/model_priority.cpp` (`render_model_priority`, native-only)
checks queue ordering, promotion, bounded priority and non-preemption.
They are **NOT RUN**, along with all native C++ builds, editor/modelview
smoke runs, Metal/Vulkan/GL/WebGL2 tests, sanitizers, screen captures,
GPU heap metrics and performance profiling, per the requested
features-first workflow. No PASS or parity claim is made.

The model footprint is not a driver memory reading; estimated-byte
admission control does not bound global GPU memory. A priority queue
does not preempt the active worker, and upload time budgets remain
advisory. Standalone modelview and raw editor preview are explicitly
separate from full authored graph/appearance simulation and need
real native verification before production use.

## Stage 103 — bounded multi-worker model imports and separate GPU resource owners

### Parallel CPU model preparation

`engine/render/AsyncModelCpuQueue.h` now supports **1–8 parallel CPU
worker tasks** with `setWorkerCount(n)` and `workerCount()` (default 1
for compatibility). Its owner-thread API still accepts
`ModelLoadPriority::Background`, `Visible` and `Interactive`,
with upgrades of queued requests, FIFO for equal priority, bounded
waiting queue, duplicate suppression, source-generation invalidation
and `pollReady(Prepared&)` for render-thread publication.
`pollReady` checks every running task rather than waiting for an older
unready task to finish, so a later independent completion can be
published without blocking. The CPU import uses `std::async` per active
job, while all GPU upload, View packet construction, ECS mutation and
model-cache publication remain outside the worker threads.

After eight consecutive selections favoring later queued work, the
scheduler admits the oldest queued source to prevent starvation of
background loads. It does not preempt active decoders, guarantee job
runtime fairness or exceed the configured concurrency cap.
`AsyncModelCpuQueue::Stats` now includes active/configured worker
counts, total dispatched work and fairness activations in addition
to the existing queued/finished/stale result counters.
`ModelAssetProvider::setCpuWorkerCount`, `cpuWorkerCount` and
`setCpuMaxQueued` expose scheduling through the native project
resource owner. The Editor Model window uses two workers and an
8-request waiting queue in its opt-in raw model preview, with
`Interactive` priority, one GPU upload per invocation and the
existing nominal 1 ms upload budget. CPU worker counts and waiting
requests are shown beside the preview when active.

**Thread-safety:** the caller-provided `FileReader` must be
reentrant/thread-safe when worker count exceeds one. `ModelTextureCache`
guards decoded-image access with a mutex, while GPU methods are confined
to the render owner thread. Calls to request, invalidate, poll and
shutdown are single-owner-thread operations. An invalidated CPU task
may finish but must not be published if its revision is stale. Lowering
worker count does not interrupt work already in flight; project
shutdown joins it before reader and texture-cache destruction.

### Owner-accounted GPU estimates without shared-resource double charging

`render::ModelTextureCache::gpuOwnerEstimate()` walks its unique
GPU texture keys ONCE and reports unique texture count, compressed
texture count and RGBA8 base-mip equivalent bytes. Borrowed
`Model::images` are not billed again because shared GPU textures have
their own resource owner. This equivalent-size estimate is NOT the
actual GPU allocation size for compressed images, mipmaps or
driver-specific alignment.

`render::ModelResourceCache::gpuOwnerEstimate()` combines *separate*
categories: known dedicated allocations of its resident models,
known retired-model allocations awaiting GPU fences, unique
ModelTextureCache GPU texture equivalents, and number of models
referencing the shared geometry pool. The global geometry pool's
allocated/occupied page sizes belong to the *device-wide* owner and
must be queried separately via `geometryPoolStats(device)`.
Do **not** sum the device-global geometry pool once per model/cache,
nor add borrowed per-model textures to the owner texture count.
`RtsView::assetResidencyStats()` and the Editor Statistics window
expose these categories separately. The numbers remain diagnostic,
source-derived approximations, not vendor GPU heap measurements.

### Additional real editor render consumer

`apps/editor/SceneRenderable.h` now routes authored
`SceneRenderable::model` through `aiverse::View::submitModel`
and normal unselected meshes through
`aiverse::View::submitMesh`. Existing `ModelDraw` transform,
tint and selected/hovered model outline fields are unchanged.
Special selected/hovered mesh drawing continues using the
renderer-specific selection silhouette path; pickable debug
edges and world-space selection behavior are retained. This
removes another direct model/mesh submission from the editor
without importing any genre-specific code into the View.
The default animated/complex RTS portrait remains a compatibility
adapter and must not acquire a second MotionWeave graph evaluator.

### Deferred verification and known limits

`tests/render/model_parallel_jobs.cpp` (CTest
`render_model_parallel_jobs`, native-only, Threads) covers bounded
multi-worker concurrency, revision invalidation, duplicate requests,
new priority classes and worker configuration.
`tests/render/model_owner_estimates.cpp` (CTest
`render_model_owner_estimates`) checks model-owned allocation
estimates exclude borrowed shared textures even across independent
retirement records. These are ordinary CMake test targets.
**NOT RUN:** CMake/compiler, CTest/run-tests.sh, desktop/mobile GPU,
parallel source import stress, editor render/selection parity, FPS/RTS,
sanitizers, benchmarks, CI and screen captures, by the user's
features-before-tests request.

The scheduler currently starts a bounded number of `std::async` tasks,
rather than keeping persistent thread-pool workers, and it does not
support live preemption. Memory accounting excludes unknown
driver allocations and must not be treated as a guaranteed budget.
Shared geometry pool accounting is device-wide, not attributable
without additional ownership tags. Remaining RtsView type appearance,
simulation-specific visuals and animated portrait wrappers are still
not retired.

## Stage 104 — persistent native model workers, shared-page identity and generic mesh selection

### Persistent bounded CPU worker pool

`render::AsyncModelCpuQueue` now runs a *persistent*, lazy-started
`std::thread` pool instead of creating a new `std::async` thread for
each imported model. `setWorkerCount(1..8)` is preserved and the
default stays one worker; no GPU devices, ECS worlds or renderer
operations are touched by the threads. The main/host thread owns
`request`, `invalidate`, `pollReady`, `setWorkerCount` and
`shutdown`; workers share a mutex/condition-variable protected
priority queue, active source revisions and a ready-to-publish deque.
Reader callbacks MUST be reentrant when more than one worker runs.

Queued sources retain the Stage 102 priorities
`Interactive > Visible > Background`, same-priority FIFO,
priority promotion without duplicate work and an occasional oldest
pending dispatch after eight priority bypasses. Worker concurrency
is bounded, existing in-flight imports are never preempted, and
source revisions prevent invalidated results from being published.
`pollReady` returns a single valid prepared result without
waiting for a particular worker; stale results can be skipped.
`setMaxQueued` limits waiting plus prepared-but-unpublished sources,
and `setMaxReady` (default 16) applies backpressure to dispatch when
the ready deque is full (up to a bounded number of concurrently
finishing jobs can overshoot it). The pool therefore does not keep
launching imports indefinitely when GPU publication is paused.

`setWorkerCount` joins any in-flight CPU jobs while reconfiguring
the pool and then restarts persistent threads when work is present:
this operation can block if a reader is slow, so it is not for
per-frame tuning. Valid finished results survive this reconfiguration.
`shutdown()` joins all worker threads, clears their results and
releases captured FileReader/texture-cache state before the project
is destroyed. `AsyncModelCpuQueue::Stats` now also reports the
ready count and number of live persistent threads.
`ModelAssetProvider` continues to publish GPU models only on the
render/device owner thread through `publishReady`; its previous
public priority/budget APIs are source-compatible.

### Shared geometry pages: real identity, no double charging

`render::geometryPoolReferencedStats(gfx::Device&,
span<const Model* const>)` counts unique `GeometryPoolPage`
identities referenced by a **combined** set of models, validating
each page belongs to the passed device. It reports allocated
and occupied page bytes once per page (plus model-reference count)
under the device's geometry-pool mutex. The page's GPU buffer is
never released, reallocated or modified by this statistics function.

`render::ModelResourceCache::geometryPoolModelReferences()`
supplies models both currently resident and pending retirement
behind GPU completion tokens. The generic retirement queue exposes
read-only `forEachPending` so a retired page remains represented
until the GPU-safe retirement actually happens.

The Editor Statistics window reports the pages referenced by the
RTS compatibility view as a **subset** of the device-wide
`geometryPoolStats(device)`, not an additional allocation to
sum with it. A page can be shared by more than one project host:
for an accurate combined report, concatenate the hosts' borrowed
Model pointers and invoke `geometryPoolReferencedStats` ONCE.
These are allocator capacity/occupancy bytes, not driver VRAM
heap measurements. Shared texture cache entries are likewise still
counted once by their resource owner, not multiplied per model.

### Canonical View selection/outline packets

`aiverse::view::MeshObject` has two additive opt-in fields,
`selectionOutline` and `selectionColor`. When enabled,
`aiverse::View::submitMesh` forwards the exact legacy selection
silhouette call to `Renderer3D::drawSelectionMesh`; otherwise it
retains the original ordinary mesh submission (including flags,
specular and emissive settings). These are genre-independent
presentation styles and do not introduce gameplay selection
components in core View.

`apps/editor/SceneRenderable.h` now sends **all** ordinary and
selected/hovered mesh submissions through the canonical View.
The original model `ModelDraw` outline remains intact; the
selected/hovered mesh silhouette colors, tint, world transforms and
draw order are unchanged. The existing pickable selection bounds
edge lines now use `View::submitLine`, while the editor still
chooses when those edges are needed based on its selection outline
pass setting. No additional MotionWeave update/evaluation occurs.

### Deferred verification

`tests/render/persistent_model_pool.cpp`, CTest
`render_persistent_model_pool`, checks bounded worker reuse,
CPU result publication, worker downsize/upsize and final shutdown.
`tests/render/geometry_page_owners.cpp`, CTest
`render_geometry_page_owners`, checks shared page identity across
two independent resource caches without creating a fake GPU device.
`tests/view/packets.cpp` has additional assertions on the opt-in
mesh selection packet (CTest `view_scene_packets`).

These tests are authored and registered but **NOT RUN**, per the
user's features-before-tests preference. Native compilation, all
CTest suites, Metal/Vulkan/GL/WebGL2, graphics parity, thread
sanitizers, editor selection and resource-lifetime hardware checks
remain unverified. Do not merge to `main` based only on source review.

**Known limitations:** persistent threads are per provider, not a
process-wide pool; changing concurrency may join a long-running
reader synchronously; the ready queue limit has a concurrency-sized
overshoot; global geometry page identities are reliable only for
models whose page is still owned on the target device. Backend GPU
completion and multithreaded source parsing still require real
platform verification. `game::RtsView` remains a compatibility
host for authored type appearances and advanced animated portraits.

## Stage 105 — cross-host resource group and fully-authored generic portrait rendering

`engine/render/RenderResourceOwnerGroup.h` is an additive, genre-free,
**non-owning** aggregator for several `render::ModelResourceCache` hosts
(e.g., a scene, a portrait, and a raw-model preview). `add(owner)`
deduplicates the same cache registration; `clear()` never shuts down
those caches. `snapshotOwners(gpuHealthy)` is CPU-only, suitable for
source-level diagnostics without constructing a gfx device.
`snapshot(gfx::Device&)` enriches it with the GPU-device-global
geometry pool and uniquely referenced `GeometryPoolPage` allocations.

Model pointers are counted once by identity across all registered
owners. Each `ModelTextureCache` contributes unique GPU image keys
only once by cache identity (not once per Model borrower).
`geometryPoolReferencedStats` counts a page once even when it is
shared by models owned by different providers; the globally allocated
device pool is included for comparison but **not added again** in the
resource group's `estimatedKnownBytes()`. Source-derived model bytes
and RGBA8-equivalent shared texture bytes are approximate and omit
driver allocations, mip/alignment, offscreen targets, unknown morph
texture height and unrelated renderer subsystems.

`RenderResourceGroupStats` reports unique hosts, live and
GPU-fence-retired Models, shared texture owners/textures, compressed
texture count, resident and retained model estimates, geometry
pool ownership, and retirement diagnostic counters. Its health
status distinguishes `Ready`, `PendingGpuRetirement`, and
`DeviceUnhealthy` (from `Device::gpuHealthy()`). Pending GPU fences
are **not** a device error. `setEstimatedBudgetBytes()`/
`mayAdmitNewSources()` provide optional **new-source admission**
control only. They never bypass a GPU completion fence, evict
resident assets forcibly, or claim to recover a lost GPU.

The editor Model window's optional raw-static-model inspection is a
concrete three-owner consumer: `RtsView::modelResources()`,
`Portrait::modelResources()`, and its own `ModelAssetProvider`
are grouped. A 256 MiB **estimated, group-wide** visual admission
budget is used in this opt-in mode, in addition to the raw provider's
existing per-owner 64 MiB estimated-retirement limit. The UI shows
owner/model counts, estimated known bytes, GPU-pending and
device-unhealthy status. If the backend is unhealthy, new raw
CPU requests and GPU publication are paused; resident visuals
remain available when the device can still present them.

**Static portrait migration:** `game::Portrait::renderAppearance(
const render::ModelDraw&,const render::RTSCamera&,float4 background)`
accepts *fully authored, borrowed* model packets, including material
overrides, node-material assignments, geometry policy, team tint,
variant and optionally already-evaluated pose. It submits through
the canonical `aiverse::View::submitModel` exactly once and draws
the offscreen target using a caller-provided camera and clear colour.
Existing native raw-model `renderStaticModel` and precomputed-pose
`renderPose` now delegate to this one generic path, retaining
their original camera/clear-colour/lighting parameters.
The RTS `Portrait::render(RtsView&, ...)` path first resolves
`StaticPreviewModel` via the legacy host but passes the resulting
full `ModelDraw` to `renderAppearance` with the SAME camera for
static cases. Animated/complex cases still use the original
`RtsView::drawPreview` fallback; there is no second MotionWeave
graph writer or extra animation tick.

**Deferred tests:** `tests/render/resource_owner_group.cpp` checks
independent owner registration, resource model identity counts,
borrowed-texture exclusion, source-derived budget threshold and
unhealthy-device admission with no real GPU. New
`tests/view/portrait_appearance_api.cpp` checks the native
fully-authored portrait API surface at compile time. Both are
ordinary registered CTest targets. **NOT RUN**: CMake compiler,
CTest/unified runner, Metal/Vulkan/GL/GLES/WebGL GPU device,
resource/fence lifetime, editor portrait visual parity, FPS/RTS
gameplay, thread sanitizers and CI; no PASS claim or production
readiness is implied.

**Caveats:** group registration is non-owning and must never outlive
any included cache; the host alone owns and shuts resources down.
The reader/GPU device/lifetime scope of all caches in a device
snapshot must match that device. `gpuHealthy()` defaults to true
on unsupported backends and is not an exhaustive GPU error probe.
The estimated budget is advisory and does not replace real backend
allocation/tracking or force release of unresolved GPU resources.
Full authored animated character/RTS view retirement is not complete.

## Stage 106 — safe resource-owner registrations and explicit failed GPU fence diagnostics

### Expiring owner registrations

`render::ModelResourceCache` now exposes a non-owning lifetime
marker via `lifetimeToken()`. `RenderResourceOwnerGroup` stores
`weak_ptr<const void>` markers alongside raw cache addresses and
checks expiration before dereferencing. A cache invalidates its marker
at the start of its destructor. `add` deduplicates *live*
registrations; a newly constructed cache reusing the same address as
an expired one is a distinct owner. `size()`, `snapshotOwners()`
and `snapshot(device)` ignore dead caches, and diagnostics report
`expiredRegistrations` without dereferencing them.

**Important thread boundary:** registration, cache destruction and
group snapshot are still device-owner-thread operations. A weak marker
alone does not provide a thread-safe GPU cache snapshot or guard
against a destructor racing with a snapshot on another thread.
`RenderResourceOwnerGroup` remains an observer, not a cache owner;
it never takes responsibility for GPU shutdown.

### Failure-aware GPU completion

`gfx::ResourceRetirementStatus` is now a three-state
`Pending / Complete / Failed` API, reported through the additive
`gfx::Device::resourceRetirementStatus(token)` virtual method.
The default maps older backends' boolean completion status to
Complete or conservatively Pending. Metal checks its command-buffer
completion callback's atomic status; Vulkan distinguishes
`VK_NOT_READY` from non-success fence errors (including device
loss); GL checks `glClientWaitSync` results and treats
`GL_TIMEOUT_EXPIRED` as Pending, `GL_WAIT_FAILED` as Failed.

`AsyncResourceRetirementQueue::collect` retains its earlier
five-callback API, and now has an overload accepting an explicit
`failed(token)` predicate. A token-confirmed failure is marked
*once* per batch; the queue reports `failedFenceBatches` and
`unresolvedFailedFences` separately from routine
`pendingPolls` or unsupported-token fallback counters. Failed
batches are sticky: they can NEVER be released by a later token
completion or elapsed time. They are kept until the backend
provides a successful full-device `waitForResourceIdle()`
barrier, whereupon `drain` can safely free the models.

For completed tokens, the existing backend
`endResourceRetirement(token)` remains valid. The additive
`abandonRetirementTokenAfterIdle(token)` is a narrowly-scoped
native marker cleanup hook invoked ONLY after a successful full
GPU-idle barrier; Metal discards callback bookkeeping, Vulkan
destroys the now-idle fence and GL deletes the idle sync object.
This does not turn a failed fence into proof of completion.
`ModelResourceCache` uses the explicit three-state probe and
fence-failure predicate when collecting retired models, and
post-idle cleanup when draining shutdown resources.

`RenderResourceGroupStats::health` now distinguishes
`Ready`, `PendingGpuRetirement`,
`RetirementFenceFailed` and `DeviceUnhealthy`.
Backend-unhealthy takes precedence; a real unresolved fence
failure prevents NEW source admission even if the device
reports healthy. The editor shows failures separately from
normal async completion and resource-pressure status; its
statistics panel exposes per-scene failed token batches.

### Additional canonical View consumer

The material-library preview in `game::Portrait::renderMaterial`
now assembles its existing authored `ModelDraw` (preview shape,
material override, team and rotation) and submits through
`Portrait::renderAppearance` / `aiverse::View`, using the same
camera, clear colour and portrait render targets. The actual
`view.applyMaterial` authoring still resides in the compatibility
adapter. This unifies another static preview with native model
and precomputed-pose consumers without introducing a second
MotionWeave animation evaluation or changing gameplay/saves.

### Validation status and caveats

New source fixtures `tests/render/resource_owner_lifetime.cpp`
(CTest `render_resource_owner_lifetime`) and
`tests/render/retirement_failed_token.cpp` (CTest
`render_retirement_failed_token`) cover weak owner expiry/
replacement and explicit failed-token retention followed by
confirmed GPU idle recovery. Normal CMake targets were registered.
All compilation, CTest/`run-tests.sh`, Metal/Vulkan/GL/WebGL2
GPU checks, editor screenshots, memory profiling, device-loss
stress and sanitizer checks are **NOT RUN** by the user's
features-first request. Source updates do not establish green
CI or production GPU lifetime safety.

Remaining limitations: weak lifetime checks do not make
cross-thread destruction safe; explicit failure states only cover
backends that implement the three-state probe; tokens cannot
include commands not yet submitted on their GPU queue; no
automatic lost-device recreation exists; group memory numbers
are source-derived estimates, not measured driver allocations.
`RtsView` is still needed for complex authored animation and
visual effects. The next agent must validate with real build and
hardware before claiming parity or merging.

## Stage 107 — scoped GPU owner registrations, replayable fence telemetry and generic appearance packets

### Explicit device/thread ownership with scoped registration

`render::ModelResourceCache::bindResourceDevice(gfx::Device&)` fixes
the cache's GPU-device identity and owner thread at its first bind.
Repeated binds to another device or thread fail with `logic_error`;
its earlier non-owning `lifetimeToken()` remains available.
`collectRetired`, `invalidateSources`, `replace`,
`trimUnused`, `publishPrepared` and `shutdown` now verify this
identity; `ModelAssetProvider` verifies it before import/upload,
`RtsView::init` before presentation model setup and
`Portrait::init` before preview GPU creation. After successful,
explicit GPU-safe cache shutdown the device binding is cleared, so
the cache may be reused by a different GPU device. Failed shutdown
does NOT clear binding or make still-in-flight GPU allocations safe.

The recommended multi-host API is now
`RenderResourceOwnerGroup::registerScoped(device,cache)`, returning a
move-only `ScopedRegistration` lease. The group binds the cache to
that device, rejects mixed-device registration, remembers its
constructing `std::thread::id` and throws on wrong-thread
`snapshotOwners`, `snapshot(device)`, `add`, `clear` and budget
changes. The lease automatically unregisters on the original thread.
An expired/destroyed group leaves a harmless weak lease; a lease
destroyed on the wrong thread refuses cross-thread mutation and
increments `wrongThreadReleaseAttempts()`. A live cache that
has been shut down/rebound is rejected by a stale scoped device
snapshot. No registration extends the model cache lifetime.

The old `add(const ModelResourceCache&)` remains as a source-compatible
CPU-only observer method for legacy tools/tests, but new GPU-facing
hosts should use scoped registration. The Editor raw-source Model
window now uses three scoped leases — world, portrait and raw-model
provider — for its group-wide estimated budget and device-health
snapshot. All registration, cache destruction and snapshot operations
MUST be serialized on the device owner thread. This does not magically
make a cache safe for concurrent GPU/CPU mutation or detect a Device
destroyed without respecting the host's lifecycle.

### Replayable synthetic GPU completion trace

`render::RetirementStatusReplay` supplies a deterministic,
CPU-only scripted timeline for opaque GPU marker tokens.
`script(token,{Pending,...,Complete/Failed})`, `issue()`,
`advance()`, `status(token)` and `events()` reproduce status
transitions in a fixed order without timers or a real gfx backend.
Unknown tokens conservatively return Failed, not completion.
This class is for testing and diagnostics, **not** a GPU fence or
an authority for actual memory destruction.

`AsyncResourceRetirementQueue` now has an optional diagnostic
observer receiving `RetirementDiagnosticEvent` events:
`Enqueued`, `TokenSubmitted`, `Pending`, `Completed`,
`Failed`, `IdleFallbackFailed`, `RecoveredAfterIdle`.
Instrumentation callbacks cannot interrupt the resource retirement
operation if they throw. A `ModelResourceCache` host may subscribe
via `setRetirementDiagnosticObserver`; no observer is installed by
default and the user owns logging/retention limits. These events are
informational and never replace explicit GPU completion checks.

### Generic authored material and graph appearance provider

New `engine/view/AppearancePacket.h` exposes
`view::AppearanceSpec` and
`view::buildAppearancePacket(spec, world, view::Object&)`.
A native host can resolve a Model pointer, material override vector,
node-material assignment, geometry policy, material variant, tint,
team, flags, source identity/generation and optionally a shared
animation graph/parameters **outside** any RTS/FPS module.
The builder generates the ordinary canonical View packet without
loading sources, evaluating an animation graph or uploading GPU
resources. For precomputed/static appearances,
`submitPrecomputedAppearance` synchronously sends the packet only
when it has no graph; graph-backed instances must instead use
one owning `View::update` followed by `submitOne`, so no graph is
evaluated twice.

`modules/rts/view/RtsStaticPreviewModel.h` now maps the existing
authored RTS static model/placement fields into the genre-independent
`AppearanceSpec`, then asks the generic builder to compose the
materials/variant/geometry/tint/flags packet. RTS still owns the
structure/air/footprint/yaw placement decision and typed resource
lookup, but **not** the common ModelDraw field construction.
The RTS static-preview packet and ghost placement paths still use
their original transforms, flags and material overrides; no dynamic
simulation or save/network state has changed. Full RTS material
library rule parsing and animated graph authoring remain a legacy
adapter pending separate migration and visual parity validation.

### Deferred verification

New CMake/CTest fixtures:
`tests/render/resource_owner_scope.cpp` /
`render_scoped_resource_owner` (native with Threads) tests
device/thread mismatch, scoped unregister, stale binding after
shutdown and safe detached lease after group destruction;
`tests/render/retirement_status_replay.cpp` /
`render_retirement_status_replay` tests deterministic marker
transitions, sticky failure, owner diagnostic events and explicit
idle recovery;
`tests/view/appearance_packet.cpp` /
`view_appearance_packet` checks typed host-authored appearance fields.
All sources are committed but **NOT RUN** under the features-first
test deferral. Native compiler/CMake/CTest, Metal/Vulkan/GL,
GUI/editor, FPS/RTS, sanitizer and GPU fence stress are unverified.

**Limitations:** per-cache GPU-device identity checks only protect
paths that go through this resource owner; bespoke render targets,
terrain/FX, external direct `uploadModel` calls and a destroyed
gfx::Device are still host lifecycle responsibilities. Scoped
registration is thread-confined, not thread-safe. Synthetic timing
is not an actual GPU completion proof; real Metal/Vulkan/GL tests
are required before merging this work. Full `game::RtsView`
removal and full dynamic material/graph host migration remain
future stages.

## Stage 108 — device-epoch-safe resource ownership, bounded retirement tracing and generic appearance authoring

### Explicit device generations

`render::RenderDeviceLifetime` is a host-owned, thread-affine
generation token for one live `gfx::Device`. Its
`Lease::validFor(device)` verifies the stored device identity,
unique nonzero generation and owner thread, and requires a live
host epoch. `RenderDeviceLifetime::retire()` must be called by
the host **after** all model caches have successfully released
their GPU allocations and **before** the gfx device is
destroyed/recreated. Expiration cannot turn an unsuccessful
GPU idle barrier into completion. The lease is not a GPU
fence and does not retain a gfx Device.

`ModelResourceCache::bindResourceDevice(device,lease)` now supports
an optional explicit epoch in addition to the Stage 107 legacy
device-pointer/thread guard. Once bound to an epoch, subsequent
cache GPU mutation requires the lease to remain valid for that
device, and another generation is rejected even when a new device
reuses the same memory address. An **explicitly successful**
GPU-safe cache `shutdown(device)` resets its device/epoch
binding; failed shutdown does not. `RenderResourceOwnerGroup`
has an overload `registerScoped(lease,cache)`, stores the
registration's generation, rejects generation mismatch and
checks for expired/rebound generations before device snapshots.

The Editor owns a `RenderDeviceLifetime` across
`Editor::init` / `Editor::shutdown`. Its Scene,
Portrait and raw-source ModelResourceCache registrations use
that same epoch lease, and it is retired only after its windows
and scene resource caches have shut down. New native hosts can
do the same. Older `registerScoped(device,cache)` and
CPU-only `add(cache)` remain source compatible but do not
provide generation protection by themselves.

### Bounded GPU diagnostics

New `render::RetirementDiagnosticRing` holds up to N metadata
events (128 by default in the Editor), reporting capacity,
retained count, total received and overflow/discard count.
Oldest records are dropped when full; a zero-capacity ring
counts discards without storing events. Its weak observer
callback cannot extend the ring lifetime, and callback
exceptions/allocation failures are isolated from GPU
retirement. This ring does **not** hold Models, GPU handles
or fence leases, and diagnostics can never prove completion
or authorize resource destruction.

The Editor attaches a 128-event ring to its RtsView Model
resource owner and displays retained/overflow counts in the
Statistics window. The trace is retained across ordinary
scene reloads and detached on editor shutdown, never kept
beyond the GPU owner's lifecycle. Callbacks and ring reads
must be on the device-owner thread; wrong-thread events are
reported, not inserted into the non-thread-safe deque.

### Independent authored materials and AnimationGraph policies

New `engine/view/AppearanceAuthoringService.h` is a
genre-independent authoring service usable from native
projects without `UnitType`, RTS/FPS gameplay rules or
`RtsView`. It provides:

- `resolveMaterial(library,reference,warn,depth)`: the same
  bounded `base`/`use` reference overlay semantics and
  warning strings, including depth protection for cycles
  and unknown material library entries
- `applyMaterial(material,library,ref,textureLoader,warn)`:
  the existing colour, metallic/roughness, emissive,
  shaderGraph, alpha, fixedLight, shadows, vertex colours,
  team channel, texture sampler references and UV overrides,
  with externally supplied GPU texture insertion
- `compileGraph(model,inlineOrSourceJson,FileReader,operations,
  traits,namedGraphs)`: inline/external graph source parsing
  and immutable `render::AnimationGraph::compile` without
  constructing a graph playback instance

The RTS adapter `RtsView::resolveMaterial` and
`RtsView::applyMaterial` now delegate to this common service
and supply their previous warning scoping, external texture
upload and material library. `RtsView::configureChoice`
compiles inline/external type-level graphs through the
service, and `RtsAnimate.cpp` uses the same compile entry
for dynamic control-authored graph definitions. **No
second MotionWeave graph clock is created**: the existing
graph instance / generic View presentation remains the
single writer of each pose. The rest of RTS gameplay
selection, procedural animation, attachments, authored
type placement and material hot reload remains with the
compatibility adapter.

### Deferred verification

New source-only CTest fixtures
`tests/render/device_epoch.cpp` (`render_device_epoch`),
`tests/render/retirement_diagnostic_ring.cpp`
(`render_retirement_diagnostic_ring`) and
`tests/view/appearance_authoring.cpp`
(`view_appearance_authoring`) cover explicit generation
changes with an identically addressed mock Device, ring
overflow/weak lifetime, material inheritance/texture-loader
semantics and external graph failure propagation. CMake
registration is included. **NOT RUN**: actual native
C++ compilation, CTest/unified runner, Metal/Vulkan/GL
real-backend execution, editor screenshots, FPS/RTS
visual parity, ThreadSanitizer and CI.

**Limits:** the epoch guard protects participating native
model caches, not arbitrary raw gfx allocations, other render
modules or a platform tearing down a Device without allowing
safe model-cache shutdown. The host must manage the lifetime
correctly; it cannot recover automatically from real device
loss. Source-derived GPU bytes are still not exact VRAM.
Dynamic authored appearances still rely on RTS-specific state
mapping/attachment policy. Real native builds and visual
tests are required before claiming compatibility or merging.

## Transactional appearance publication (Stage 109)

Native applications can use `engine/view/AppearanceAssetRegistry.h` to stage
model material overrides and an authored `AnimationGraph` independent of any
RTS/FPS/editor module. The registry copies materials and node-material maps,
owns the immutable graph program, and publishes a new `shared_ptr<const Asset>`
only after material/shader-graph validation and AnimationGraph compilation
succeed. A compile or texture-resolver failure leaves the previous live Asset
and its dirty flag intact. For material `shaderGraph` overrides, strict
prevalidation prevents the older best-effort `applyMaterial` fallback from
silently replacing a working shader with an empty program.

Call `invalidateSources({path})` at a safe frame boundary after changing a
model/graph/material-library source listed in `Request::sources`. This marks
dependent appearances dirty without throwing away their previous snapshots.
A changed external graph path forces recompilation and increments animation
generation; a material-only rebuild preserves the previous graph and its
animation generation so a running MotionWeave source does not restart.
`Request::graphRevision` is for changes to in-memory graph inputs with the
same JSON source. Every new graph-driven asset needs a stable nonzero
`sourceId`. The returned Asset's `packet(world,out)` transfers its
source identity, graph, authored parameters, material pointers and flags into
the same universal `view::Object`; the host calls **one** `View::update`
before submission. Never create another independent animation clock.

A caller must keep the snapshot handle alive through synchronous submission,
and the imported `render::Model`/GPU resources must outlive all snapshots
borrowing them. Registry snapshots do not own models, texture handles,
gfx::Device or render-thread fences. Texture loader callbacks may have external
GPU side effects that the host must roll back/retire; publication is atomic
for the registry map, not a GPU transaction. Registry mutation remains on the
owner presentation thread.

The FPS executable now calls the common
`AppearanceAuthoringService::compileGraph` for its reference character
controller. It also owns an explicit `RenderDeviceLifetime` while that
standalone client uses its GPU resources, guards character loading and
shutdown against a different/expired generation, and retires the token after
known resource shutdown. The FPS scene graph still uses its existing
presentation ECS and animation hot reload; it has no second MotionWeave
writer. Other native hosts and direct postFX/terrain/FX GPU allocations
have not yet been migrated to generation-aware owners.

Deferred fixture `view_appearance_registry` covers shader-compile rollback,
material library invalidation, pinned snapshots, animated parameters/source
identity, stable graph generation on material-only rebuilds, and external
graph source invalidation. **NOT RUN**: native compile, CTest, GPU visual
parity, device-loss stress and sanitizer validation.

## Stage 110 — shared dynamic appearance choices and FPS hot reload

`engine/view/AppearanceChoiceAuthoring.h` resolves `materials` (`*` first,
then named layers), `assign` (case-insensitive `*`/`?` node patterns with
descendant inheritance, later rules overriding earlier assignments), and
`variant` into a pure CPU `AppearanceChoicePlan`. No GPU call, gameplay
state, renderer ownership or animation clock exists in that resolver. It
retains the layering order, including the `replace` material flag. The
legacy RTS `RtsView::configureChoice` now consumes this plan but keeps
RTS team-tint decisions, dynamic stages, LOD, weapons, IK, sockets,
procedural animation and texture upload in its compatibility adapter.

`AppearanceAssetRegistry::Request::appearance` can now consume the
same authored material/node/variant description as RTS. The registry
applies named/wildcard layers and newly generated node-assigned
materials inside its strict publication transaction, with copied
material overrides and a preserved last-good snapshot. Explicit
`materialOverrides`, `nodeMaterial` and `variant` request fields
still support native host-provided overrides. A `modelRevision` from the
host additionally rejects cached graph reuse when an imported model
has been replaced at the same memory address. Invalidating the old
Model source (its `Model::path`) also forces graph recompilation;
the revision and invalidation mechanism **does not** own any GPU
resource and must be driven before model eviction.

The standalone FPS remote-character path now actually publishes the
initial graph, materials, node assignments and variant via
`AppearanceAssetRegistry`, and supplies those snapshot-owned values
to its `render::ModelDraw`. Project-authored
`fps.visual.materials`, `fps.visual.assign` and
`fps.visual.variant` follow this shared policy, with optional
`fps.visual.materialLibrary` for named material definitions.
A non-empty `fps.visual.graphFile` is still validated to remain in
the project. Its timestamp is polled every 30 frames: when changed,
the registry invalidates the source and compiles the external
AnimationGraph through the same generic service. Failed edits retain
the last-good graph, last-good material snapshot and ECS playback
state, and an error is logged. On successful graph replacement the
existing FPS presentation ECS asset components receive the graph and
restart their existing players; no second View/MotionWeave graph clock
is introduced. The FPS network/authority simulation remains
unaffected.

**Lifetimes:** Keep `AppearanceAssetRegistry::Snapshot` alive through
all synchronous draw submission; clear these snapshots before GPU
model shutdown. Each FPS player still owns its presentation source
identity independently of the registry's authoring key. Shader graph
CPU validation is strict; GPU shader-program compilation and texture
upload callbacks are still host responsibilities and not atomically
reversible by the registry. The registry is not a filesystem watcher,
and FPS currently polls `graphFile`, not arbitrary material JSON
files. The RTS adapter remains responsible for material texture GPU
residency, IK, graph runtime state, attachments, and LODs.

Deferred `view_appearance_choice` validates shared named/all
materials, descendants, extra node materials, snapshot packet values,
model revision and model-source invalidation. Stage 109
`view_appearance_registry` still covers last-good reload behavior.
**Neither has been run**. Native CMake, CTest, sanitizer, GPU
resource/device-loss and screenshot parity checks remain pending.

## Stage 111 — RTS appearance snapshots and shared host GPU epochs

The generic `AppearanceAssetRegistry` now has
`publishPrepared(key, Asset, error)` for native authoring hosts that already
resolved material textures and compiled an AnimationGraph. It publishes an
immutable, host-pinned snapshot **without** reloading textures, recompiling
the graph or creating another animation runtime. A rejected snapshot keeps
the old asset/dirty flag intact. It validates non-null Model, a full base
material set, node material indices and an explicit source ID for an
animated asset. It does not make host GPU side effects transactional.

RTS `reloadAppearanceTypes` stages replacement TypeLooks and their
prepared snapshots locally, keeping all previous looks in place until every
requested resident type has completed staging. Each model, LOD and
construction/damage stage gets its own snapshot. A strict reload path
prevalidates material-library references, shader DAG programs and external
AnimationGraph definitions. Invalid external graph content or an invalid
material shader program rejects the staged type before that choice's
additional texture overrides are uploaded. On a successful publication,
`RtsView::drawObject` uses the immutable snapshot's material overrides,
node material assignments, GeometryPolicy and variant. The host still owns
GPU texture indices and the underlying imported Models.

Compiled graph signatures are taken from the **parsed** JSON definition,
including re-read external graph contents instead of the file path alone.
When only `materials.json` is reloaded and every model choice, LOD and
stage retains compatible graph/model/runtime shape, the old compiled
graph pointers are reused and the running per-entity
`AnimationGraphInstance` / MotionWeave clock remains intact. If a graph
signature or model changes, the affected animation sources are forgotten
and rebuilt as before. Per-type eligibility is conservative; all modeled
choices/stages/LODs must match. The simulation remains authoritative.

`RtsView::shutdown` now detaches generic View source handles, RTS
animation state and TypeLooks (including their snapshots) **before**
`ModelResourceCache::shutdown` begins GPU retirement. The original cache
wait-for-idle safety requirement is unchanged.

A native `GameClient` creates one `RenderDeviceLifetime` in
`initStep(0)`, binds its `RtsView` model-cache resources to that epoch
during `initStep(2)`, and retires the epoch only after shutdown of its
render resource owners. For an editor-embedded GameClient, the editor
passes its **own** generation-aware lease through
`EditorClientPlay -> ClientHost -> GameClient -> RtsView`. The hosted
client neither constructs nor retires a separate GPU device epoch;
the Editor owns the generation for the entire shared gfx::Device.
External mismatched/expired generations fail closed.

**Limitations:** The RTS transaction is atomic at the TypeLook/snapshot
publication boundary, not at the RHI: a texture loader can still add
images/handles to a cached Model during staging. Strict preflight rejects
known authored ShaderGraph/graph-reference errors early, but failures
in texture upload or later GPU pipeline compilation still need a
host-side GPU resource rollback/retirement transaction. Other postFX,
terrain, effects and render targets are not yet lifetime-guarded by
the model-cache epoch; no real device-loss recovery is claimed.

The deferred fixture `view_appearance_prepared` checks prepared publication,
pinned old snapshots, generation preservation and failure rollback.
The existing `render_device_epoch` fixture was expanded to cover
multiple cache owners sharing one editor-hosted generation. **NOT RUN**:
CMake configure/build, CTest, native editor/gameplay, GPU screenshot
parity, sanitizers or hardware-fence/device-loss tests.

## Stage 112 — transactional model image uploads and GPU generation fences

The generic `engine/render/ModelImageUploadTransaction.h` adds an
owner-thread Model image import transaction. A native authoring host
creates `ModelImageUploadTransaction(device, persistentRollbackPool,
deviceLifetimeLease)` at a render-frame boundary. It accepts decoded
`render::Image` objects through `stage(model,image)` and returns their
**future** material indices; the live `Model::images` array is not
modified until `commit()`. Multiple materials referencing the same
filename in a single Model share a staged index, and several different
Models can participate in the same transaction. Before commit, every
affected Model's image vector reserves its final capacity, so an
allocation failure cannot leave earlier Models partially appended.
Successful commit moves already GPU-uploaded Images into their Models
without a second upload. Callers must not submit staged material
indices to `Renderer3D` until the transaction has committed.

If a stage fails or the transaction is destroyed uncommitted, the
temporary Images go to a persistent
`render::ModelImageRollbackPool`. That pool uses the same
`AsyncResourceRetirementQueue` completion token semantics as the
Model cache: Pending and Failed GPU markers **never** authorize
texture destruction; a successful full-device idle permits recovery
and cleanup. At a safe frame boundary the host calls
`rollbackPool.collect(device)`; before device shutdown it must
call `rollbackPool.drain(device)` and reject teardown when the
completion barrier fails. In an exceptional queue-allocation failure,
the pool may need a synchronous idle fallback or conservatively leak
an orphaned GPU allocation until the actual device is destroyed
rather than freeing in-flight GPU work. Diagnostics expose pending
images, enqueue failures and unrecoverable batch counts.

`RtsView::reloadAppearanceTypes` now uses this transaction for all
GPU material images added during candidate construction across
every selected type, model variant, LOD and damage/construction stage.
Its texture loader stages in the transaction instead of immediately
calling `addImage`; missing/undecodable textures or failed GPU
creation abort the candidate set. After all CPU authoring checks and
prepared appearance snapshots succeed, `commit()` publishes the
Model images before the TypeLook/snapshot swap. On exceptions before
commit, live Model image counts, last-good appearance snapshots and
running animations remain unchanged. The existing separate
`RtsView::collectRetiredModels()` now polls both model and rolled-back
image queues. The GameClient and Editor frame boundaries explicitly
invoke it whenever **either** queue has pending resources; an
image-only failed hot reload must not strand its retirement tokens.
The Editor Statistics panel reports pending rollback images, queue
allocation failures and unrecoverable orphan batches. Shutdown drains
the image queue after detaching borrowed snapshots and before
shutting down the Model resource cache.

Generation-aware checks were extended to `render::PostFx`,
`TerrainRenderer`, `RtsGroundFx`, `fx::ParticleSystem` and
Editor Scene/Game `Viewport`, using the SAME editor/client epoch
already established in Stage 111. The particle renderer refuses an
invalid device at initialization and stops emitting draw commands
after an epoch expires. Its destructor-safe shutdown deliberately
avoids calling an expired device; orphan handles may then remain
until device teardown. Epoch validity is a **safety check**, not a
GPU synchronization fence or automatic device-loss recovery.

**Still not universally transactional:** initial model import,
other render-target/terrain shader/FX allocations, texture cache
sources, and runtime postprocess pipeline changes continue to have
their own GPU ownership policies. The stage covers new material
Images routed through the RTS hot-appearance texture loader, not
all RHI allocations in the engine. The new native-only deferred
`render_model_image_transaction` test checks abort, duplicate
staging, multi-Model commit and failure-to-idle GPU token behavior.
No native CMake build, CTest or real backend validation was run.

## Stage 113 — transactional scratch Models and shared GPU texture aliases

The Stage 112 `ModelImageUploadTransaction` now also participates in a
**scratch Model** transaction for RTS authored appearance hot reload.
`RtsView::reloadAppearanceTypes` supplies a temporary
`ModelResourceCache::ModelMap` to its `loadModelCached` adapter.
Uncached choices/LODs/stage alternatives/attachments are decoded and
uploaded into that map, without inserting them into the live Model
cache or adding failed-load tombstones before the entire appearance
transaction succeeds. Repeated keys within the transaction reuse the
same scratch Model and preserve the Model* used by staged attachments.

Speculative `uploadModel(device,model,keepCpu,
publishSharedTextures=false)` reuses **already resident**
`ModelTextureCache::borrowResident(image)` GPU leases, including
the current texture revision and sampler key. A missing key receives
a transaction-owned dedicated texture: it does **not** insert a GPU
entry into the shared cache. The shared decoded-pixel LRU can still
receive CPU entries while staging; these do not imply GPU
publication. Existing alias leases outlive the speculative Model
until its GPU retirement completes. New shared GPU cache entries
are not promoted by this stage; this is a deliberately conservative
GPU-residency boundary, not complete shared-cache transaction support.

After all requested TypeLooks and immutable appearance snapshots
are prepared, the image transaction commits and
`ModelResourceCache::publishStagedModels(device,models)` validates
the bound GPU generation and resident-key uniqueness, then transfers
whole `std::map` nodes into live storage (no new Model allocation;
borrowed Model* addresses remain stable). On precommit failure,
an owner RAII scope sends each scratch Model through
`retireUnpublished` into the ordinary model GPU fence queue.
Partially uploaded buffers, owned textures, baked animation
textures and morph textures are released by `releaseModel` only
after a completed native token or a proven full-device idle.
A failed retirement queue allocation attempts an explicit
idle barrier; if unavailable, the code deliberately retains an
orphaned GPU allocation for device teardown rather than releasing
potentially in-flight memory. Counters surface this condition.

The generic `AsyncResourceRetirementQueue::enqueueFrom(resource)`
preserves caller ownership if a queue vector allocation throws.
The rollback image pool uses this API too. Retirement metadata
now has `RetirementResourceKind::Model/Image`, written by
editor observers into the same bounded ring; event tags are **not**
GPU synchronization or permission to destroy resources. Editor
Stats distinguishes model and image events, plus scratch queue
failures and orphan Models.

The canonical `Renderer3D` now accepts the same optional
host-controlled `RenderDeviceLifetime::Lease` as PostFx and VFX.
It checks this lease at init, shutdown, `beginScene`, `render`,
and `resolvePostFx`. Editor, GameClient (including editor-hosted)
and FPS propagate their **existing**, single device epoch into
Renderer3D. The check prevents use of an expired/recycled
Device pointer; it is neither a completion fence nor automatic
recovery after device loss.

Deferred `render_model_image_transaction` now additionally covers
resident shared texture aliasing, missing-key dedicated fallback,
unpublished Model retirement and scratch Model map publication;
`render_retirement_diagnostic_ring` checks Model/Image event tags.
**NOT RUN:** native CMake, CTest, real GPU timelines, screenshots,
sanitizers and cross-platform parity. This stage is source-only
and must remain Draft until validated.

## Stage 114 — shared texture promotion, GPU shader preflight and offscreen retirement

The generic `ModelTextureCache::prepareGpuPromotion(models, appended)`
builds a two-phase GPU ownership plan from already uploaded scratch
Models **and** material images whose future append slots are still held
by `ModelImageUploadTransaction`. The plan allocates shared lease control
blocks and cache map nodes without modifying live shared GPU residency.
After the outer appearance/image/Model publication passes all checks,
`PreparedGpuPromotion::commit()` transfers preallocated map nodes,
converts source images into borrowed shared leases, and returns duplicate
dedicated texture handles for `ModelImageRollbackPool::retire`.
No additional GPU texture upload is performed during promotion.
A rejected plan never publishes a new cache key. Keys include the
existing normalized source, sampler and source revision policy.
To remain correct when `Model::images` reallocates, the plan stores
**Model pointer + image index** rather than pointers to vector elements.

During RTS authored hot reload, `RtsView::loadTexture` uses the shared
CPU-decoded `ModelTextureCache::read` and stages its GPU image outside
the Model. The complete GPU promotion plan combines those future
material images with scratch imported Model images. Texture duplicates
across both sources share one GPU owner after success; no cache misses
are published on failure. `uploadModel(...,publishSharedTextures=false)`
retains the source cache identity on dedicated speculative misses and
rejects missing GPU texture allocations rather than publishing a partially
uploaded Model.

GPU ShaderGraph authoring now has an **optional host-renderer preflight**.
`Renderer3D::warmModelPipelinesChecked` returns
`PipelineWarmStatus::Ready / Pending / Failed` rather than conflating
asynchronous compiler progress and invalid shader/pipeline handles.
`RtsView::setAppearancePipelineValidator` invokes the host's single
Renderer3D preflight for each staged choice, LOD and damage/construction
stage before committing Model/texture/TypeLook changes. Failed GPU
programs keep the previous live appearance and animation clocks. For
Pending, the editor leaves the source watcher unacknowledged and retries
on a later scan; no second animation writer is created. Already warmed
shader/pipeline cache entries are still owned by Renderer3D and are not
a separate GPU rollback transaction.

Native `ModelAssetProvider` now uses a reusable, genre-neutral
`ModelStagingTransaction` for both synchronous `load()` and
asynchronous `publishReady()`. It holds newly imported Models in a
scratch map, uploads them with shared cache GPU publication disabled,
then promotes the shared textures and Model map nodes on success.
Failures return through the same GPU-fenced speculative Model retirement
and legacy failed-load tombstone policy, rather than immediately
destroying partially uploaded GPU buffers/textures. A native host must
call `collectRetired` at safe frame boundaries and `shutdown` before
destroying its gfx::Device. An optional host lease binds the shared GPU
device generation.

Editor Scene/Game `Viewport` now uses an optional weak
`OffscreenTargetRetirement` owner for color/depth textures. Resizing
or destroying a window enqueues the previous pair instead of deleting
possibly in-flight GPU targets. Editor owns one pool under its existing
GPU generation, collects at the beginning of frames, and requires a
successful GPU idle drain after windows are destroyed and before retiring
the editor GPU epoch. Pending/Failed GPU fence tokens never authorize
destruction. This pool and other Model/Image queues share the editor's
bounded retirement diagnostic stream, now including `Target` events.

The deferred `render_model_image_transaction` fixture includes
cross-Model shared GPU deduplication, pending material image slots,
vector relocation, generic native scratch commit/abort, and failed
offscreen target GPU retirement. **NOT RUN:** native CMake/CTest,
Metal/Vulkan/GL/WebGL2 runtime, UI visual parity, device-loss recovery,
sanitizers or CI. This is a source implementation with unverified
compiler/backend behavior, not merge-ready GPU code.

## Stage 115 — NEREID integration and fence-backed GPU program retirement

The animation feature branch incorporated the latest reviewed `main`
tree through merge commit `01538817a68ac869b1e9ca12bc48ef9d9f4d56c7`
(two real parents: animation Stage 114 and NEREID main).
Of 114 main-changed paths, 108 were disjoint and six files
(`AGENTS.md`, `CMakeLists.txt`, `PostFx.h/.cpp`, and
`Renderer3D.h/.cpp`) were merged from independent hunks.
The resulting tree was compared to the previous feature tree:
exactly 114 files changed, preserving NEREID/underwater features
and the animation host's GPU epoch guards. This confirms Git
ancestry and source inclusion, NOT native compatibility or tests.

`render::GpuProgramRetirement` owns a persistent, genre-neutral
`AsyncResourceRetirementQueue<GpuProgramBatch>` containing
shader and pipeline handles. When `Renderer3D` changes its
material-quality tier or GPU instance limit, old cached GPU model
programs are enqueued instead of destroyed immediately. The host
calls `Renderer3D::collectRetiredPrograms()` at an actual frame
boundary (outside a render pass). Pending/Failed completion tokens
retain shader and pipeline handles. A proven device-idle barrier
can drain the queue at `Renderer3D::shutdown`; failure prevents
silently freeing in-flight GPU resources. If queue allocation fails,
the owner attempts a true GPU idle fallback and records any
unrecoverable/orphan batch.

The editor, standalone/embedded `GameClient`, and FPS host poll
this program retirement before frame submission, all using the
already existing single host `RenderDeviceLifetime` generation.
`Renderer3D::hasPendingModelPipelineJobs()` and
`pollPendingModelPipelineJobs()` also allow completed asynchronous
native shader/pipeline jobs to progress without rebuilding a
candidate Model/texture set. During an Editor hot appearance reload,
a previous `Pending` GPU compiler result holds the source
watcher unacknowledged and skips repeated scratch uploads while
those specific jobs are in flight. When no jobs remain, the
candidate is re-evaluated normally; `Failed` continues to
retain the last-good appearance and AnimationGraph clocks.

On clients/editor that provide a GPU generation lease,
`PostFx` now owns a GPU-fenced `OffscreenTargetRetirement` pool.
Target LRU eviction, HDR/size format replacement, LUT replacement
and the final texture shutdown enqueue old handles rather than
destroying textures still potentially used by prior passes.
The host collects these handles at frame boundaries and
`PostFx::shutdown()` drains with a proven GPU-idle barrier
before releasing shader and pipeline handles. Standalone legacy
tools that omit a generation lease retain their original
immediate-destruction contract. This is a resource-retirement
change only: the NEREID underwater composition and depth-sampling
logic added by main are preserved.

The speculative `render::uploadModel` path now checks that
indexed vertex/index GPU allocations really succeeded BEFORE
clearing source vertex/index arrays. A failed partial upload
therefore fails the surrounding scratch transaction instead of
appearing successful after the CPU arrays were discarded.

Native-only deferred fixture `render_gpu_program_retirement`
covers Pending, Failed, unsupported-token/idle recovery, shader
and pipeline release ordering and stale GPU generations. It is
registered with CMake but has **NOT BEEN RUN**. Neither native
CMake/CTest nor Metal/Vulkan/OpenGL rendering, sanitizer,
NEREID/animation coexistence, ShaderGraph compilation or CI
has been executed for this stage. PR #22 remains Draft.

## Stage 116 — GPU-fenced shadow, reflection and trace targets

`Renderer3D` now maintains ONE `OffscreenTargetRetirement` queue using
the existing host-owned `RenderDeviceLifetime::Lease`. It is created
before renderer GPU allocation and borrowed by `LightSystem`; no
independent device epoch or genre-specific target allocator exists.
`retireRenderTargets(color,depth)` queues replaced handles at
render-owner frame boundaries. The Editor, GameClient and FPS host
collect only when `pendingTargetRetirements()` is nonzero, prior to
new submissions, using real `gfx::Device` retirement tokens.
Pending/Failed tokens are never proof that GPU work has completed.

The following render targets are now covered:
- `Renderer3D::setShadowQuality` shadow-map depth replacement, with
  last-good resource preservation on failed new allocation
- `Renderer3DTrace::beginSceneCapture` SSR scene color/depth resize
  and LRU eviction, including partially allocated attachments
- Editor depth-tested scene-selection mask resizing
- `LightSystem` map/depth quality changes, spot-shadow quality
  changes, static radiance/point-light atlas replacement, and
  planar water reflection color/depth changes (including NEREID)
- All of the above owners' normal shutdown paths

When allocating a replacement render target fails, the previous
live handles remain usable where an old target exists. A partially
allocated new pair is retired safely rather than destroyed during
a possibly in-flight pass. `Renderer3D::shutdown` now requires a
successful full-device idle barrier before freeing live GPU buffers
and pipelines, then drains the target and program retirement pools
before retiring their owner generation. The LightSystem borrows
the renderer target pool only until its own shutdown is complete.

`Renderer3D::warmModelPipelinesChecked` also tracks outstanding
backend `warmPipelineColor(RGBA16F)` operations in a separate
`pendingColorWarms_` list. `pollPendingModelPipelineJobs()` advances
these existing handles along with ShaderJob/PipelineJob completion.
The Editor source watcher therefore keeps an appearance edit pending
without repeatedly importing scratch Models/textures merely because
HDR pipeline colour warmup remains incomplete. The device API
exposes only a boolean readiness for colour warmup; a permanent
backend error can remain Pending rather than being classified Failed.
The Editor Stats view exposes queued shaders, pipelines, HDR
colour variants, completed colour variants and the target
retirement orphan counters.

**Deferred/not run:** the existing native
`render_model_image_transaction` fixture now covers shadow-only,
SSR and reflection target pairs, a partially allocated target, a
Pending/Failed fence and a valid completed token. It also includes
a failed vertex/index buffer creation check. These tests and all
CMake/CTest/Metal/Vulkan/OpenGL/sanitizer suites are **NOT RUN**.
The implementation has not yet been compiled or validated on GPU.

## Stage 117 — explicit HDR GPU warmup status (source-only, 2026-11-19)

`gfx::Device::PipelineColorWarmStatus` and optional `warmPipelineColorStatus`
provide Ready/Pending/Failed for backend colour-target shader compilation.
The original bool `warmPipelineColor` remains source-compatible: older
backends default to Ready/Pending only and cannot claim definitive failure.
The Metal backend now reports Failed for rejected HDR shaders/pipelines,
missing source handles and invalidated base pipelines; it reports Pending
only while a native shader/pipeline job is still incomplete.

`Renderer3D::warmModelPipelinesChecked` rejects a Failed HDR variant and
retains the previously published View appearance. The renderer tracks
pending and terminally failed pipeline handles separately, clearing both
on cache invalidation and preventing repeated polling of a failed variant.
`GpuWarmStats::colorVariantsFailed` is shown in editor statistics.
Neither the MotionWeave presentation clock nor the NEREID/Prisma owners
are modified. This code is **NOT COMPILED OR TESTED**; PR #22 stays Draft.
Full details: [Stage 117 handoff](agent-handoffs/animation-stage117-hdr-warm-status-2026-11-19.md).

## Stage 118 — partial GPU initialization rollback

Renderer3D initialization is now transactional: a failed native shader,
pipeline, vertex/index buffer, model fallback texture, shadow target,
LightSystem or trace resource triggers safe partial teardown after a
successful Device::waitForResourceIdle() barrier. The renderer rejects
reinit over a live owner and refuses to discard outstanding retirement
queues. Main, blend, shadow, line/triangle pipelines and buffers and
fallback textures are checked before publication. A failed idle barrier
throws rather than deleting in-flight GPU resources.

LightSystem validates every necessary map, spot, halo, decal and beam
GPU handle, rolls back unsuccessful initialization (including standalone
hosts), and guards invalid handles during shutdown. Standalone target
release without a borrowed retirement owner now requires proven idle;
otherwise it intentionally retains an orphan and exposes counters.
Trace initialization stops at the first failed allocation; renderer
rollback cleans resources already created. These changes preserve the
single View/MotionWeave presentation owner and do not change NEREID
reflection policy or Prisma's independently owned PostFx targets.

**NOT RUN:** compile, link, CTest, GPU/sanitizer or visual parity.
See [Stage 118 handoff](agent-handoffs/animation-stage118-gpu-init-rollback-2026-11-19.md).

## Stage 119 — hot reload continuity and orphan-safe RTT fallback

Generic `aiverse::View` still updates one presentation ECS graph
clock per source. `MotionWeaveSourceBridge` transfers a
`PlaybackSnapshot` when an authored graph or model changes without
changing source generation or compatible bone ancestry. The new
pose is re-evaluated after reset, avoiding stale model matrices;
LOD draws and static attachments remain synchronous `View::submitModel`
packets. Changed source generation, graph state/node identity, and
incompatible rig hierarchies still use restart semantics.

The exceptional unleased `Renderer3D::retireRenderTargets` fallback
now requires `gfx::Device::waitForResourceIdle` success before
destroying live GPU textures. Otherwise it retains/orphans those
handles and counts failed barriers. Normally Renderer3D owns the
fenced `OffscreenTargetRetirement` and this fallback is unused;
Editor Stats exposes fallback failures. The separate
`RenderTexture` canvas transient upload/draw buffers require
a follow-on GPU lifetime refactor (Stage 120); the current file
was audited but not modified in Stage 119.

**NOT RUN:** native build/CTest, sanitizers or graphics regressions.
See [Stage 119 handoff](agent-handoffs/animation-stage119-playback-continuity-2026-11-19.md).

## Stage 120 — generic GPU canvas retirement and host frame polling

`render::CanvasGpuRetirement` provides device-generation-aware GPU
fence retirement for temporary render canvas buffers, staging images,
texture targets, shader and pipeline programs. This is independent
of FPS/RTS and does not own a second renderer, Device epoch or
MotionWeave clock. The legacy `RtsPaintLayers` adapter borrows the
same pool for all canvases and cached meshes; Editor and GameClient
poll that shared pool at safe pre-render frame boundaries.

Canvas destruction and paint source changes no longer free GPU-visible
handles immediately, and GPU mesh upload failures preserve last-good
geometry. A standalone RenderTexture may create its own pool and
drain on explicit shutdown; unsupported fence backends may use a
single genuine idle barrier at collection. Error/fence/orphan metrics
are visible in Editor Stats. NEREID, TerraWeave, Prisma and
SpectraFX resource owners remain separate.

**NOT RUN:** CMake, native C++ compilation, CTest, GPU validation or
sanitizers. [Stage 120 handoff](agent-handoffs/animation-stage120-canvas-gpu-retirement-2026-11-19.md).

## Stage 121 — whole-asset LOD, source rig and generic socket attachments

Canonical `aiverse::View` now has opt-in whole-model LOD and socket
attachment composition, independent of FPS/RTS and separate from the
existing renderer's per-mesh projected-pixel LOD.

- `engine/view/ModelComposition.h` exports
  `view::ModelLod`, `selectModelLodLevel` (deterministic distance
  hysteresis), `compatibleModelPose`, `view::ModelAttachment`
  and `view::composeAttachment`. Native projects can reuse these
  directly without creating any genre-specific simulation.
- `view::Object::animationModel` holds the canonical source rig for
  one MotionWeave graph. `draw.model` remains the base render asset.
  Optional `lods` provide additional distance-selected visual Models;
  changing the selected visual model never resets/steps the graph.
  The selection is cached by stable object ID + generation and pruned
  as objects leave the View.
- A different visible Model receives current `Pose`/node matrices
  only when its hierarchy/rest/skin layout matches the source.
  Different morph targets cannot be blindly remapped across models.
  An incompatible visual LOD uses its own bind pose while the source
  graph clock and source-rig sockets continue correctly.
- `view::Object::attachments` are synchronous ModelDraw packets
  anchored by named/indexed node on the **source** animated rig,
  not by potentially simplified LOD nodes. No extra graph player.
  The RTS weapon/cargo/banner adapter now calls the same
  `view::composeAttachment` math after its host-owned gameplay filters.
- All new Object fields are appended after the original fields,
  preserving existing aggregate source initialization order.
  Lifetimes of Model/Graph/Material/GPU resources remain host-owned.

A separate canvas reliability change makes `RenderTexture::upload`
return success/failure; categorical and territory paint layers preserve
CPU source bytes/dirty flags for retry when GPU upload fails.
See [Stage 121 handoff](agent-handoffs/animation-stage121-universal-lod-attachments-2026-11-19.md).

**NOT RUN:** CMake compiler/linker, CTest, GPU visual parity, sanitizers
or performance benchmarks. This source-only stage is not release validated.

## Stage 122 — explicit visual rig retargeting and prepared-packet frame lifecycle

The canonical `aiverse::View` accepts optional `ModelLod::retarget`
maps from `engine/view/ModelPoseRetarget.h`. An explicit, target-indexed
mapping identifies which source animation node drives each visible
LOD node; -1 preserves that visual node's bind TRS. Visual skeleton
differences are supported through target bind + mapped source-local
animation deltas, not by incorrectly copying source joint palettes,
skin inverse binds or morph data. Morph remapping is explicit and
target-indexed; unmapped targets keep their own bind weights.
Invalid mappings fall back to the visual bind pose while the base
animation source and sockets keep advancing independently.

`View::submitOne` now also recognizes a host-precomputed Pose and
nodeWorld matrix array, provided `graph` is null and
`animationModel` identifies the authoritative source. This is
the normal FPS character path: the FPS presentation ECS runs once,
`View::beginPreparedFrame` maintains LOD hysteresis IDs without
advancing graphs, then the FPS client calls `submitPackets`.
For a View-owned graph use `View::update` once instead; NEVER
call both update and beginPreparedFrame for the same source frame.
Existing per-mesh Renderer3D LOD is unchanged.

The RTS adapter constructs a validated name-based map once in
`RtsLook` and supplies it to the same retarget helper when a
separate LOD asset is selected, while preserving the previous
legacy world-matrix fallback for unsupported authored LODs pending
visual parity. No gameplay or animation authority was migrated
to the renderer.

**NOT RUN:** native compiler/build/CTest, real GPU backend visual
tests, sanitizers or benchmarks. Review [Stage 122 handoff](agent-handoffs/animation-stage122-rig-retarget-2026-11-19.md).

## Stage 123 — precompiled asset-generation retarget plans for crowds

The genre-neutral `aiverse::View` now supports immutable
`CompiledModelPoseRetarget` plans for whole-model visual LODs.
`compileModelPoseRetarget` verifies authored target-to-source
node/morph mappings, source/target bind transforms and scales at
the asset publication boundary; it stores a compact step per
visual bone. `retargetCompiledModelPose` applies only current
source Pose deltas and builds the current visual pose/world
matrices; there is no repeated ancestry check or temporary
visited-node validation allocation per actor.

A compiled plan is tied to the **nonzero, host-owned publication
generations and exact pointers of BOTH Models**. Generic
`view::Object::animationModelGeneration` and
`ModelLod::visualModelGeneration` must match the plan. Bumping
either generation on hot reload rejects the old plan even if
the allocator reused a pointer; the visual remains in bind
pose until a replacement plan is published. Legacy callers
using only `ModelLod::retarget` still use the slower, checked
runtime path. Source MotionWeave graph, pose authority and
source-rig attachments remain unchanged.

`View::modelRetargetStats()` reports compiled draws,
stale/rejected plans, exact pose reuse, legacy validation and
bind-pose fallbacks per presentation frame. RTS compiled LOD
draws are a separate legacy adapter and not counted here.
No performance increase is claimed without benchmarking.

**NOT RUN:** native compile/link, CTest/CI, Metal/GL/Vulkan,
sanitizers or FPS/RTS/editor visual parity. See [Stage 123 handoff](agent-handoffs/animation-stage123-compiled-retarget-cache-2026-11-19.md).

## Stage 124 — reusable, reentrancy-safe LOD pose scratch

`retargetCompiledModelPose` now assigns a compiled visual bind
Pose into caller-owned output storage and computes target
`worldMatrices` in the caller's reusable vector. Unlike the
previous temporary-return path, actor submissions may reuse
capacity across a large sequence of retargeted models.

`aiverse::View` now owns one `RetargetScratch` (Pose + mat4
vector) per presentation View, avoiding a fresh pair of
temporary vectors per ordinary submit. An RAII busy guard
handles nested `Renderer3D::drawModel` capture callbacks by
using isolated stack scratch for the nested call. Renderer3D
consumes pose and matrix pointers synchronously, while
persistent GPU-scene uploads copy the world/bounds/identity
data; no delayed GPU command references retarget scratch.

Render-owner-thread-only and after ECS worker join remains
mandatory. The older noncompiled retarget helper still
allocates validation storage, and no benchmark speedup
is claimed. A failed helper return means caller must
ignore scratch contents and draw target bind pose.

A new `makeNamedModelPoseRetarget` maps visual nodes to
source bones by name at asset import and
`describeModelPoseRetarget` reports rejected authored
hierarchy/morph cases, without per-draw diagnostics.
[Stage 124 handoff](agent-handoffs/animation-stage124-pose-scratch-authoring-2026-11-19.md).

**NOT RUN:** CMake/native compiler/CTest, GPU backend visual
tests, sanitizers or profiling.

## Stage 125 — bounded CPU Pose retention and global Model generations

`aiverse::View` now bounds *retained* transient Pose/matrix
capacity: 512 KiB per View by default, configurable with
`setRetargetScratchBudgetBytes` (0 means no retention).
Large animated models are still drawn at full detail; excess
scratch capacity is freed after the synchronous model draw and
nested capture callbacks finish. `retargetScratchCapacityBytes`
and `ModelRetargetStats::scratchTrims` expose diagnostics.
This does not cap peak per-draw allocations or alter MotionWeave
graph clocks, game simulation or GPU retirement ownership.

`view::ModelPublicationEpoch::next()` provides nonzero,
monotonically increasing asset publication identities across
the native rendering host. `AppearanceAssetRegistry`
`publish` and `publishPrepared` issue a new revision and
`Asset::packet` forwards it through
`Object::animationModelGeneration`. Retarget plans must
still be compiled for the current source AND visual revisions;
changed or missing generations, changed model pointers and
bad plan shape receive separate `CompiledRetargetStatus`
reasons and are diagnosed without walking the skeleton.
Any invalid compiled plan uses the visual's own bind pose.

The RTS compatibility adapter offers an opt-in bind-pose
legacy LOD fallback while preserving old visuals by default.
Editor Stats shows its fallback counts and the generic View
scratch retention limit. See [Stage 125 handoff](agent-handoffs/animation-stage125-scratch-budget-model-epochs-2026-11-19.md).

**NOT RUN:** native C++ compiler/CMake, CTest, GPU, sanitizers,
screenshots or benchmarks. Source-only stage; no speedup claimed.

## Stage 126 — separate Model revisions from appearance/graph hot reload

Published appearance `version`, animation graph `generation`
and `modelPublicationGeneration` now have distinct semantics.
The generic `AppearanceAssetRegistry` preserves the Model
publication epoch across successful graph-only or material-only
republication when the borrowed Model pointer, host
`modelRevision` and source path remain the same. It marks a
separate `dirtyModel_` on `invalidateSources(modelPath)` so an
in-place reimport with pointer reuse invalidates stale compiled
LOD retarget plans. Graph-source invalidation alone does not
invalidate the source Model. Publication failure leaves the
last-good snapshot and dirty state intact.

Native projects without the registry can set
`AppearanceSpec::modelPublicationGeneration` and call the
common `buildAppearancePacket`; registry `Asset::packet`
uses this same path. For an opt-in whole-model LOD, build an
immutable compiled plan at the publication boundary with
`Asset::compileVisualLod(visual,minDistance,map,out,error)`.
The helper requires both snapshots' nonzero Model generations
and keeps `out` unchanged if compilation/validation fails.
Host-owned Models, GPU buffers and the source animation World
are NEVER owned by the publication snapshot.

The FPS adapter supplies the published Model generation to
its already evaluated remote-character packets. Compatible
graph-file-only hot reload can preserve state and clip phases
while resetting old montage/notify/contact state; no duplicate
MotionWeave update is introduced.

**NOT RUN:** native compiler/link, CMake, CTest, FPS/RTS/editor
visual parity, GPU backend tests or sanitizers.
See [Stage 126 handoff](agent-handoffs/animation-stage126-published-lod-hot-reload-2026-11-19.md).

## Stage 127 — Model-publication-aware MotionWeave bridge and pinned native batches

`AnimationSourceFrame::modelPublicationGeneration` now carries
the host's Model asset revision into the presentation ECS
`AnimationSourceLinkComponent`, separately from the actor's
logical entity `generation`. `View::update` forwards
`Object::animationModelGeneration`, and `View::submitOne`
only uses an evaluated graph-owned Pose when the last synced
source ID, logical generation, Model epoch, graph and Model
pointer are all consistent.

When a hot reload changes a known nonzero Model epoch, the
bridge restarts the presentation player without migrating old
clip/state clocks, even if the C++ pointer has been recycled.
The bridge rejects an in-place Model change where the old
compiled AnimationGraph pointer is reused with a new Model
epoch, BEFORE mutating the World. A compatible graph-only
reload with constant Model epoch may still migrate named
playback. Zero remains a legacy Model-epoch opt-out.

Native/cinematic projects can use `PinnedAppearanceBatch`
to hold immutable `AppearanceAssetRegistry::Snapshot`
objects and their `view::Object` packets through one
`View::update(batch.span(),dt)` +
`View::submit(renderer,batch.span())` transaction. This
protects borrowed material/graph snapshot pointers against
registry replacement but DOES NOT own the underlying
Model or its GPU resources. The host still needs a joined
animation worker barrier, Model lifetime pin and real GPU
retirement before destruction. Avoid editing/clearing a
batch during synchronous submission.

No authoritative gameplay changes, new graph clock or GPU
ownership were added. See
[Stage 127 handoff](agent-handoffs/animation-stage127-model-epoch-bridge-pinned-appearance-2026-11-19.md).

**NOT RUN:** CMake/native compiler, CTest, real GPU
rendering, sanitizer or performance tests.

## Stage 128 — opt-in owned native appearance batches

`view::PinnedAppearanceBatch` now supports two complementary lifetime
modes. Legacy `add(snapshot,world)` retains immutable appearance
materials and AnimationGraph snapshots but borrows the raw Model
from its native host. New `addOwned(snapshot,world,shared_ptr<const
Model>)` additionally retains a **real strongly-owned CPU Model**
whose pointer must exactly match the published snapshot.

After optional `edit(index)` adds LODs or attachments, the host may call
`pinRelatedModel(shared_ptr)` for each referenced Model. The batch
deduplicates shared CPU model owners across actors. Call
`missingModelPins`, `requireModelPins` or `validatedSpan()` to
check all base/animation/LOD/attachment Model pointers are pinned.
The additional `validatedSpan(device, gpuLifetime.lease())` overload
verifies the host's current GPU Device epoch and owner thread, not
GPU command completion. Use the same span for exactly one
`View::update` and subsequent synchronous `View::submit`, then
call `batch.clear()` after workers have joined and submission ended.

**Critical:** CPU shared ownership does not make a Model's GPU vertex
buffers/textures safe to destroy. GPU lifetime remains under the host's
fenced Model retirement and Device owner. Native
`ModelResourceCache` still owns `unique_ptr<Model>`; never fake
ownership with a non-owning shared_ptr. Existing cache-backed hosts
retain their explicit detach/evict/retire contract until owner-approved
reader leases are implemented. No animation clocks or genre rules
changed.

**NOT RUN:** native build/CTest, GPU validation, screenshots,
sanitizers, large crowds or memory benchmarks.
[Stage 128 handoff](agent-handoffs/animation-stage128-native-model-lifetime-pins-2026-11-19.md).

## Stage 129 — cache reader leases for native View transactions

Generic `render::ModelResourceCache` retains its `unique_ptr<Model>`
and independent GPU-fenced retirement, but now provides real
owner-thread CPU read guards. `ModelReadLease` pins a specific
resident cache Model against eviction/replacement, while
`ModelCacheReadScope` guards an entire legacy multi-model frame.
While guards are active, `ModelResourceCache::invalidateSources`,
`replace`, `trimUnused`, and `shutdown` fail closed rather than
destroying CPU Model objects that MotionWeave / Renderer3D are
still reading. A host must release every guard before cache teardown;
destruction with live guards is a fatal ownership error.

Native code can assemble `PinnedAppearanceBatch` using genuine
`cache.acquireModelRead(model)` tokens passed to
`batch.addCacheOwned(snapshot,world,std::move(lease))`. Visual
LODs, base animation rigs and attachments use
`pinCacheRelatedModel`. `validatedSpan` checks CPU owners from
either real `shared_ptr<Model>` pins or cache read leases, and
can optionally validate a GPU Device epoch lease. The Model
itself remains cache-owned; the View batch does not adopt the
Model, allocate GPU buffers or alter the animation clock.
`batch.clear()` releases the read tokens only after the joined
animation update and synchronous draw.

The legacy RTS adapter takes a coarse cache read scope after its
upfront model loads in `RtsView::draw`, covering animation
evaluation and scene submission. Editor Stats reports active
cache readers when any exist. GPU command completion STILL
requires the owner's independent deferred retirement queue;
a read lease or GPU Device epoch cannot replace a real GPU fence.
[Stage 129 handoff](agent-handoffs/animation-stage129-model-cache-read-leases-2026-11-19.md).

**NOT RUN:** CMake/native compiler, CTest, real GPU backend,
hot reload fault-injection, sanitizers or benchmarks.

## Stage 130 — cache read lease owner rebind and diagnostic counters

`ModelResourceCache`'s GPU owner and CPU Model read owner are
related but independent. Read guards still block cache mutation
while a View evaluates a Model or submits its synchronous
draw. After a successful `ModelResourceCache::shutdown`
(no active reader, GPU idle proved and Model resources freed),
the former read-owner thread ID is reset: the host may
reinitialize the empty cache against a NEW Device owner
on a different render thread. No existing reader is ever
rebound.

Both `ModelReadLease` and `ModelCacheReadScope`
are strictly owner-thread-only. Releasing either on
another thread terminates rather than allowing cache
eviction to race with borrowed data use. The cache
exposes `modelReadSafetyStats()` with active readers,
historical peak readers and cumulative blocked mutation
attempts (`invalidateSources`, `replace`, `trimUnused`,
`shutdown`). RTS forwards this to Editor Statistics;
no simulation ECS state or GPU fence owner changes.

This is NOT a cross-thread reader-writer lock or GPU
completion fence. Host Model data must remain immutable
while read, and MotionWeave workers must join before
the owner releases its guard. See
[Stage 130 handoff](agent-handoffs/animation-stage130-read-lease-lifecycle-diagnostics-2026-11-19.md).

**NOT RUN:** native compiler/CMake/CTest,
GPU visual parity, sanitizers or benchmarks.

## Stage 131 — streamed Model publication while View is reading

`ModelResourceCache::acquireModelRead(key)` now verifies the owner
thread and active cache lifetime BEFORE accessing its non-threadsafe
Model map. A foreign-thread lookup rejects with `logic_error`;
this is not a cross-thread reader-writer lock and does not make
the underlying Model memory concurrently mutable.

`RtsView::draw` continues to guard Model lifetime with a
coarse `ModelCacheReadScope` acquired after upfront type
loads and held until synchronous Renderer3D submission returns.
NEW Model keys may still be published by the owner thread
using `insertIfAbsent`, `publishPrepared` or
`publishStagedModels`, preserving the streaming design.
Replacing, trimming, invalidating or shutting down the
unique-owned cache while readers are active fails closed.

The existing native CTest `render_device_epoch` has new
CPU-only regression scenarios for lease lifecycle, denied
mutations, new-key streaming and owner-thread rebind.
These source tests are **authored but NOT RUN**; no measured
runtime verification or GPU frame safety is claimed.
See [Stage 131 handoff](agent-handoffs/animation-stage131-streaming-cache-read-regressions-2026-11-19.md).



## Stage 149 — animated identity and LOD publication safety

Generic `aiverse::View::update` requires an immutable animation source Model and a **stable nonzero** `Object::id` whenever `Object::graph` is supplied. It rejects anonymous Graph+Model objects before animation ECS update; graphless model draws, precomputed poses and ordinary meshes remain valid without an ID. The `Object::generation` is logical source identity, independent of `Object::animationModelGeneration` for authored asset publication.

The View's per-ID Model LOD hysteresis now invalidates when that source generation/publication changes, the last selected visual LOD model address/visual epoch/threshold changes or the number of LOD levels changes. Source detach via either `forgetSources` or `forgetSourcesForReimport` drops the visual LOD cache entry. Render metadata compares old addresses without dereferencing retired resources. For generic `ModelAttachment` draws, View-generated `persistentId` values incorporate the parent's Model-publication epoch as well as logical identity and index; explicit host-provided IDs are retained. LODs do not start extra animation graph instances or tick a pose source.

These are source-level changes; the full authoring/retargeting integration still needs final native validation. **NOT RUN:** compile, CTest, real Metal/GLES/Vulkan, screenshots, performance/sanitizers. Details in [Stage 149 handoff](agent-handoffs/animation-stage149-source-identity-lod-attachment-reimport-2026-11-19.md).



## Stage 150 — exact complete Model LOD revisions and compiled attachment sockets

Generic `aiverse::View` now tracks **all** authored `view::ModelLod` entries for hysteresis invalidation, including currently unselected levels. Comparison-only metadata records each LOD Model address, asset publication generation, threshold and retarget/compiled-plan identity, plus the base Model/source rig and original asset revision. No old Model pointer is dereferenced. An edited LOD or changed hysteresis configuration resets the previous selection; the new revision vector is allocated only when the authored configuration changes. Native hosts should benchmark high-crowd memory/CPU during the final validation pass.

For frequently repeated named or indexed child-model sockets, an authoring/import step can call `view::compileModelSocket(*sourceRig, nonzeroPublicationEpoch, "Hand.R", -1)`, retaining a shared immutable `CompiledModelSocket` on each `ModelAttachment::compiledSocket`. `View::submitOne` supplies the current `Object::animationModelGeneration` and checks the compiled binding against that rig, nonzero publication epoch, authored socket name and optional index before using the cached node index. A mismatch skips the attachment instead of silently using an old skeleton's bone. If no compiled socket is provided, earlier runtime name/index composition remains supported. No game-genre logic or additional animation graph evaluation is introduced.

The source Model is still owned by the host; compiled socket objects and cached LOD metadata are NOT CPU Model read leases or GPU retirement tokens. Recompile bindings after importing a replacement rig. Source code and exact commits: [Stage 150](agent-handoffs/animation-stage150-complete-lod-signatures-compiled-sockets-2026-11-19.md). **NOT RUN:** CMake/compiler/CTest, Metal/GLES/Vulkan parity, stress or sanitizer tests.



## Stage 151 — atomic authored LOD and socket binding publication

Native hosts can now compile a complete presentation configuration on a published `AppearanceAssetRegistry::Asset` through `compilePresentationBindings(lodRequests, socketRequests, out, error)`. An `Asset::VisualLodRequest` supplies a published visual Asset, increasing finite distance and a shared immutable `ModelPoseRetarget` map; an `Asset::SocketBindingRequest` supplies a unique attachment index and named/indexed socket definition. All LOD plans are compiled with the source and visual Models' nonzero publication epochs, and all sockets with the source's epoch. Any invalid entry aborts without modifying `out`.

The resulting `Asset::CompiledPresentationBindings::apply(View::Object&)` requires the same source Model and published epoch plus matching socket selectors, then replaces all LODs and compiled sockets in one packet copy/move. Older socket caches are cleared before installing the current bindings. Compiled Model pointers are comparison-only and cannot extend CPU/GPU lifetime. Previously existing uncompiled sockets/LODs and all generic rendering APIs remain supported. This authoring contract is independent of genre, simulation ECS, gameplay or network state, and does not evaluate animation. See [Stage 151 handoff](agent-handoffs/animation-stage151-transactional-presentation-authoring-2026-11-19.md). **NOT RUN:** CMake/C++/CTest, current CI green, Metal/GLES/Vulkan, runtime/visual parity, benchmarks or sanitizers.



## Stage 152 — native published packet and importer-name LOD mapping

`AppearanceAssetRegistry::Asset::VisualLodRequest` now appends `matchByName` (false by default). When explicitly true and `mapping` is null, the importer creates a source-to-visual `ModelPoseRetarget` using exact node **names**, never array indices; it then validates and compiles that mapping against both published immutable Model epochs. Complex rigs/morph channels still require explicit authored maps.

`Asset::packetWithBindings(world,span<ModelAttachment>,bindings,Object&,error)` is a transactional presentation packet builder. It produces the already-published base appearance fields, copies caller child model attachments, optionally validates and installs Stage 151 compiled LOD+socket bindings, and replaces its output only on complete success. It does not retain GPU/CPU Model ownership or evaluate an animation graph. For View-owned graph players, pass this packet to View::update once. For a native host with its **own** MotionWeave presentation World, attach its precomputed pose and set `packet.graph=nullptr` before submitting to generic View; never create a second evaluator.

The reference native FPS client now uses this published packet path for real material/geometry/Model-publication fields instead of recreating everything by hand, but has not yet imported authored separate LOD/child asset Models. Editor Modeling/Graph preview is still an independent AnimationGraphInstance owner, not migrated. See [Stage 152 handoff](agent-handoffs/animation-stage152-native-appearance-packet-integration-2026-11-19.md). **NOT RUN**: native compile/CTest, real Metal/GLES/Vulkan, FPS/editor pixel parity, sanitizers and crowd benchmarks.



## Stage 153 — real imported child Models and frame-bound read leases

Common native projects may use `engine/view/ImportedAttachmentAuthoring.h` to import real static child files via `render::ModelAssetProvider::load`, publish them with the existing `AppearanceAssetRegistry`, and compile named/indexed socket bindings against a live published parent Model. The source and every child must belong to the same `render::ModelResourceCache`. No RTS/FPS/gameplay types exist in the API. `ImportedAttachmentAuthoring::compile` holds short-lived owner-thread read leases during socket compilation, preserving last-good compiled layout on failure.

`ImportedAttachmentAuthoring::prepare` produces a move-only `Prepared` with a `View::Object`, strong appearance snapshots (for materials) and fresh `ModelReadLease` guards for the parent, every real child and optional visual LOD. Keep `Prepared` alive until `View::submitOne/submitPackets` synchronously returns, then release it on the cache owner thread before invalidating models. The model provider and separate GPU retirement queues still own CPU/GPU resource lifetime; read leases are NOT GPU fences. A stale/missing cache-resident resource fails the transaction instead of submitting dangling Models.

`AnimationOwner::ViewEvaluates` (View-owned Graph) or `HostPrecomputed` (graph explicitly cleared for an existing external Pose owner) makes single-writer behavior explicit. Animated child models should be emitted as their own View Object rather than attached as a silent extra Graph. This is generic source implementation, not full editor/FPS authored child integration; [Stage 153 handoff](agent-handoffs/animation-stage153-imported-child-attachments-read-leases-2026-11-19.md) lists exact commits and remaining work. Native compile/CTest/Metal/GLES/Vulkan, sanitizer, failure injection and high-crowd benchmarks **NOT RUN**.



## Stage 154 — ModelView imported attachment authoring consumer

The existing native `aiverse_modelview` application now integrates the generic `ImportedAttachmentAuthoring` API, rather than only leaving attachment authoring as an unused library helper. It can load a real static child Model on a named source socket through `--attach child.glb@SocketName`, read/write `--attachments-file file.json`, and manage the layout through an Attachments window (toggle, attach, detach, rebuild, save). Parent and child Models live in the SAME generic `ModelAssetProvider` cache. New authored configurations are compiled at the next Device-owner preframe boundary, preserving the previous compiled layout if a new imported child/socket is invalid. The parent Animator computes pose once; `ImportedAttachmentAuthoring::prepare(...,HostPrecomputed)` gets per-model CPU read leases, and `View::submitOne` draws its socket children without ticking another AnimationGraph.

Note: **Detach** removes a child from the authored layout but leaves its Model resident in the provider cache until normal retirement/shutdown. Rebuild uses current cached Models; it does not re-read changed child file bytes, since proper source-file hot reimport must coordinate CPU and GPU barriers. AIVERSE Studio Modeling/AnimationEditor previews are still separate owners and have NOT been integrated. Source code and limitations: [Stage 154 handoff](agent-handoffs/animation-stage154-modelview-attachment-workflow-2026-11-19.md); user workflow: [ModelView attachments](modelview-attachments.md). Actual native compiler/CTest/Metal/GLES/Vulkan/visual parity/sanitizer/perf **NOT RUN**.



## Stage 155 — Studio attachment editor with staged on-disk Model reimport

The generic `ImportedAttachmentAuthoring` system is now used from a **real AIVERSE Studio attachment authoring window** (`apps/editor/windows/AttachmentAuthoringWindow.cpp`). The native window registers automatically with the Editor's window registry in the Look workspace. It can edit a project-relative parent Model and child Model/socket list, toggle/Detach entries, Load/Save JSON and explicitly **Reimport & Publish**. Modeling and AnimationGraph toolbars can open it via **Attachments**, without sharing their directly-owned GPU Model pointers or starting another animation evaluator.

Each Studio publication first imports the entire source and child set into a new, separate `ModelAssetProvider`/ModelResourceCache bound to the current Editor GPU Device generation. This is a real file reread and compiled socket validation; failed import or socket validation retains the last-good published session. A **full GPU idle proof** is deliberately required before replacing the old provider and retiring its GPU handles. This is conservative **blocking** authoring; the existing provider and file-backed JSON metadata do NOT implement in-viewport 3D attachment visualization or deferred nonblocking hot reload yet. Nonproven GPU resources are deliberately held alive instead of freed; investigate with the eventual fence fault-injection tests.

There is no gameplay/genre logic in the new shared code; the studio editor is only an asset host and the existing `AnimationGraphInstance` preview owners remain independent. Read [Stage 155 handoff](agent-handoffs/animation-stage155-studio-attachment-staged-reimport-2026-11-20.md) and [Studio attachment guide](studio-attachment-authoring.md). **NOT RUN:** CMake/compiler/CTest/current CI passing, actual Studio launch, Metal/GLES/Vulkan visuals, GPU-fence/sanitizer/crowd tests.


## Stage 156: Studio offscreen animation attachment composition

The Studio `MotionWeave — Attachments` tab now renders the real compiled
source/child Models using its own `Viewport`, `Renderer3D`,
`ImportedAttachmentAuthoring::prepare` and generic `aiverse::View::submitOne`.
A transient preview pose is evaluated from the source Model's first imported
clip (or bind pose) and submitted as `AnimationOwner::HostPrecomputed`,
without registering another graph writer. Owner-thread parent/child
`ModelReadLease` guards remain alive throughout the synchronous renderer
pass. The source and child resources are NOT borrowed from other editor tabs.

The Studio's prior ModelAssetProvider sessions are deferred on publication
and released after a verified full-GPU-idle barrier at the retention limit or
shutdown. This is a practical bounded ownership queue, NOT yet a
per-provider async GPU token retirement solution. Source SHA list,
limitations and test backlog: [Stage 156 handoff](agent-handoffs/animation-stage156-studio-attachment-preview-deferred-retirement-2026-11-20.md).
**NOT RUN:** native CMake/CTest, GUI screenshots, Metal/Vulkan/GLES,
GPU-fence failure injection, sanitizers and performance.



## Stage 157 — ModelView atomic on-disk parent + child Model hot reload

The native `aiverse_modelview` example can now re-read the **actual source and child Model files** into a newly owned ModelAssetProvider, re-create each old parent Animator against the new Model (matching clip by exact name and optionally preserving normalized phase), publish a fresh AppearanceAssetRegistry and compile all named attachment sockets before switching the visible Scene generation. Invalid Model import/GPU upload/retarget/bone/socket refuses the candidate and leaves prior live source+attachment draw layout unchanged. The generic View still consumes a precomputed pose with `AnimationOwner::HostPrecomputed`; it does not evaluate the same graph twice.

The ModelView Attachments window has **Reload model files** (unlike cached-layout **Rebuild**) and optional **Watch source files**. CLI `--watch-model-files` polls parent/enabled child file modification times about once per second and schedules reload on the NEXT render-owner pre-Device-frame boundary. Old parent instances/Animators, appearance metadata and provider GPU resources are retained in a whole-generation queue until an explicit GPU-idle proof, capped by advisory 8 generations. On no GPU completion it intentionally quarantines GPU owners rather than freeing them; a true per-provider async GPU fence solution is still missing. See [Stage 157 handoff](agent-handoffs/animation-stage157-modelview-atomic-model-file-reimport-2026-11-20.md) and [ModelView guide](modelview-attachments.md). **NOT RUN:** native compile/CTest, real Metal/Vulkan/GLES rendering, file watcher/manual reload visual tests, sanitizer and high-crowd performance.



## Stage 158 — common scene/prefab-ready attachment layout and affine matrices

Shared `engine/view/AttachmentLayout.h` defines `AttachmentLayout`, `AttachmentLayoutItem`, and `AttachmentLayoutCodec`, a genre-independent CPU JSON schema with source parent path and ordered static child entries (Model path, named socket, enabled) plus full column-major affine `local` and `childWorld` transforms. Old JSON without these fields reads as identity; malformed/nonfinite/projective matrices abort read without changing the last-good parsed layout. `ImportedAttachmentAuthoring::childFromLayout` turns a document item and published child snapshot into the SAME runtime compiled socket request used by both ModelView and Studio; no extra AnimationGraph is evaluated.
 
ModelView now reads/writes this complete JSON and can open `--attachments-file file.json` alone using the saved parent path. Studio reads/writes the same schema and edits local socket translation plus full local/child rotation/scale/shear basis. The existing View composer uses `parent.world * socketPose * local * childWorld`, so authored matrices are not decorative UI data. This does NOT automatically instantiate scene/prefab ECS objects or own CPU/GPU assets; those remain host responsibilities. See [Stage 158 handoff](agent-handoffs/animation-stage158-universal-attachment-layout-transform-2026-11-20.md) and [layout schema](attachment-layout.md). **NOT RUN:** CMake/C++/CTest/real Metal-GLES-Vulkan, UI roundtrip/visual, sanitizer or VRAM benchmarks.


## Stage 159 — ECS scene instance identity for genre-neutral static attachments

Generic `engine/view/EcsAttachmentScene.h` builds real **presentation ECS Entities** for source-neutral attachment scene/prefab documents. Parent is an existing host-owned `aiverse::Entity` in an isolated presentation `World`; `SceneAttachmentRootComponent` stores publication generation and instance revision, and `SceneAttachmentChildComponent` stores each child Entity's parent, authored slot, model source, named socket, visibility and complete affine matrices. Disabled children still exist for authoring/restore bookkeeping, but do not import/draw. Runtime source/child compiled Models are owned by the host's ModelAssetProvider, not stored as ECS or GPU serialization.

`EcsAttachmentScene::publish` preflights the complete layout and all enabled child appearance/ModelReadLease/socket bindings before allocating any World entities, commits the source's root as the last ECS write, then destroys only old owned child entities. `save/restore` roundtrip the exact pointer-free Stage 158 AttachmentLayout document and rehydrate fresh Entity generation handles; `forget` removes only exact parent/instance-revision child handles, never a parent owned by another scene. `valid/prepare` check all generations, bound source publication and child authored state before handing a graphless HostPrecomputed packet to generic View — no second animation writer.

AIVERSE Studio's attachment authoring/3D preview now USES this presentation World and bridge for real child creation, reimport and deletion. **Excluded:** the separate `engine/scene` SceneLoadCoordinator isn't in this animation branch and automatic native game-scene activation/unload isn't wired; do not claim authoritative ECS save/load or animated child Graph integration. See [Stage 159 handoff](agent-handoffs/animation-stage159-presentation-ecs-attachment-prefab-restore-2026-11-20.md). **NOT RUN:** C++/CMake/CTest, actual Studio/Metal/Vulkan/GLES, sanitizer or high-crowd resource tests.



## Stage 160 — stable authored child keys and SceneAttachmentHost

The universal `AttachmentLayoutItem` now supports an optional stable child `id` (unique nonempty key, max 128 characters). `AttachmentLayoutCodec` roundtrips it, validating duplicates/type/length with transactional failure and retaining old files that lack IDs. The ID is an authored **referent**, not a permanent `Entity` handle: `EcsAttachmentScene::resolveChild` validates current ECS parent/child revision before returning an updated generation-safe child Entity after reimport.

New `engine/view/SceneAttachmentHost.h` manages multiple attachment scene/prefab instances keyed by scene instance ID and stable object key in ONE independent cosmetic presentation `World`. `activate` validates/creates real child ECS entities and compiles socket bindings against published Models; `deactivate/unload/clear` delete only owned child entities; `save/saveScene` produce pointer-free authored JSON; `prepare` pins the current imported source/child models through synchronous render. Studio's attachments tab now exercises this host for one preview scene and native ModelView preserves/edits authored child IDs.

The actual `SceneLoadCoordinator` resides only on peer branches `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration`, **not in the animation branch**. Its authoritative `SceneObjectAsset::id` / `SpawnPrefab` callback can be mapped to visual parent keys after reviewed integration but has not been connected here. No duplicate graph player, gameplay ECS mutation or genre-specific logic is introduced. See [Stage 160 handoff](agent-handoffs/animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md) and [host adapter guide](scene-attachment-host.md). **NOT RUN:** native C++/CTest/Metal-Vulkan-GLES/sanitizers/visual/perf.



## Stage 161 — independently animated child sources, separate from static sockets

Shared `AttachmentLayoutItem` now optionally marks a child with `animated:true` and a host-resolved `animationGraph` asset. Such an enabled child needs a stable authored child `id`. `EcsAttachmentScene` still owns its ECS child Entity but **skips** animated children when building the parent's static ModelAttachment packet. This prevents rendering it as a parent-pose child while also evaluating its own Graph.

New `view::AnimatedSceneAttachmentSources` uses a dedicated presentation `World` and source-neutral `MotionWeaveSourceBridge`, mapping `sceneId/objectKey/childId` to a monotonic child Source ID and the current child ECS Entity generation. `sync` validates real published parent+child Model cache leases, compiled child Graph and named source socket, then advances each child Graph ONCE per owner frame. `prepare` composes its own animated ModelDraw world transform from parent bone matrices plus two authored affine transforms, borrows its evaluated child Pose, clears `Object.graph`, and returns a move-only ModelReadLease scope valid through synchronous View rendering. Use a separate render-only View for prepared child objects if the parent uses colliding object IDs.

Studio now edits independent child Graph source paths, actually publishes the compiled Graph from imported Model via AppearanceAssetRegistry, evaluates child players in pre-draw EditorWindow::update and draws them in the existing 3D viewport. ModelView persists but intentionally refuses enabled animated child mode until its own independent child Graph owner exists. No authoritative gameplay ECS, duplicate updater, genre semantics or GPU ownership is introduced. See [Stage 161 handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md) / [public API guide](animated-scene-attachments.md). **NOT RUN:** native C++/CTest, Studio/ModelView, Metal/Vulkan/GLES, sanitizers and performance.
