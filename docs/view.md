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
