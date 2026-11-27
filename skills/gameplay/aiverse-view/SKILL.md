---
name: aiverse-view
description: Build genre-independent AIVERSE scene View and reusable animation/renderer host adapters.
---
# AIVERSE View

Always prefer `AIVERSE::View` and `<engine/view/View.h>` for native game
or editor scene presentation. `aiverse::View` does not know FPS,
RTS, RPG, AI, weapons, resource orders or network protocols.

Host-specific modules translate their state to
`aiverse::view::Object` (ModelDraw, source ID/generation, optional
AnimationGraph, generic input parameters) or
`aiverse::view::MeshObject` (Mesh, transform, color, flags).
Call `View::update(objects, dt)` and
`View::submit(renderer, objects)`, or use `submitOne` /
`submitMesh` on a host-prepared packet.

`MotionWeave` is the ONE shared animation system in a separate
presentation ECS World. Reuse its graph, IK, retarget, clip and
scheduler algorithms for all genres. The game host alone owns
gameplay meaning and authority.

The old `game::GameView` is a legacy compatibility adapter for
terrain/fog/roads and rich procedural effects; do not put new
common rendering functionality in it. Migrate reusable features
toward engine View, keeping old behavior until parity is proven.

Read `AGENTS.md`, `docs/view.md` and
`docs/agent-handoffs/animation-universal.md`. After EVERY
implementation stage commit an independent handoff describing
exact code SHA, paths, architecture/API, tests actually run or
NOT RUN, limitations, and next agent's specific action.

## Visibility, layers and surfaces

`AIVERSE::view_visibility` and
`<engine/view/VisibilityField.h>` provide the reusable
`aiverse::view::VisibilityField`, independent of RTS/FPS.
Hosts generate their own visibility bytes; the View effect
only blends/uploads/renders that presentation field.
Call `init(gfx::Device&)`, `setLook(VisibilityLook)`,
`build(device,width,height,heightSampler)`, then
`update(span<const uint8_t>,width,height,time)` and
`draw(renderer,time,lowTier)` on the renderer thread.
Call `shutdown()` before destroying the graphics device.
Game compatibility aliases remain in
`engine/view/game/GameFog.h` and should not be imported
by new general-purpose code.

Use `SceneLayerProviders.h` to collect host-owned
visibility, terrain, road/shore, lighting and other
opaque render passes into `ViewLayerQueue` in strict
draw order. `ScenePacketBatch` contains ordinary models,
meshes, lines and triangles. `Surface.h` combines
generic height samplers and optional normal estimation.
Never put gameplay state/authoritative visibility into
these presentation components.

## Visibility and generic scene layers

Use `engine/view/VisibilityField.h` and
`AIVERSE::view_visibility` for a reusable GPU visibility
effect. A game module provides only the visual byte mask,
display settings, size, time and a scene height sampler.
The field builds its sheet, blends the mask, uploads its
texture and draws on the render thread. This does NOT
authorize gameplay visibility or targeting. The host
owns source-of-truth vision data and calls `shutdown`
before the graphics device is released.

Use `ViewLayerProviders` and `ViewLayerQueue` to
compose scene passes with externally owned callbacks.
Collect and flush synchronously **in the same frame**.
Never cache callbacks capturing stack references or
execute GPU passes on the MotionWeave worker pool.

`ScenePacketBatch` supports generic Object, MeshObject,
LineObject, TriangleObject and CircleXZObject, with no
gameplay action, order, weapon, network or genre values.

For camera culling reuse `engine/view/Frustum.h` and
`view::SideFrustum`, supporting generic AABB and sphere
bounds. Do not copy the same frustum algorithm into
terrain, planet or game-specific renderers. This is
presentation culling, NEVER authoritative collision.

For curved worlds use `engine/view/Horizon.h` and
`view::sphericalCapHiddenByHorizon` with geometry
bounds supplied by the host. It is genre-independent
presentation culling only. Do not duplicate the
planet's horizon formula or put game planet types
in the View library.

For regular rectangular surface meshes, reuse
`engine/view/GridGeometry.h` / `appendQuadGridIndices`
instead of duplicating its tessellation loop. The host
owns all vertex heights/materials, normal mapping, any
LOD skirts and optional winding flip.

When orienting triangular surfaces relative to a host
normal, reuse `engine/view/TriangleFacing.h` and
`view::orientTriangleTo` instead of duplicating
degenerate checks, winding reversal and normalization.

## Transactional animation preparation

For a multi-owner frame, call `View::prepareUpdate(objects, dt)` to get an
independent candidate. It copies the source bridge, presentation components
and LOD state and detaches mutable AnimationGraphInstance players recursively.
Discard on failure; publish with the host's `unique_ptr<View>::swap` only after
all owners prepare. Retain immutable Model/Graph leases through submission.
This API does not roll back external callback side effects or arbitrary shared
host components; host callbacks must be read-only outside supplied state.
A World copy or PlaybackSnapshot alone is not a complete animation checkpoint.

For game selection markers, set `selectionOutline` in game.json through `set_manifest {"path":"selectionOutline","value":false}` to disable the selected and hovered model silhouettes. Verify with `game_info`. Author custom marker rings through the existing overlay data.


For project order feedback, use an event overlay with `event.on = "order"` or a view script
handler `view.on("order", fn)`. The payload includes kind, target, queued flag and 3D position.
For an in-place structure upgrade set `view.showUpgradeTarget` on the source type. The active
production item's progress drives the target type's existing construction appearance.
See `demos/order-upgrade`, `docs/models.md` and `docs/scripting.md`.


For an independent HUD or asset preview, reuse `view::ModelPreview` with
`view::PortraitOptions`; do not add an Animator or another model loader.
Provide the host's content reader and subject tint, prepare before submission,
and shut down through the resource provider before destroying the device.
For data-driven HUD portraits use `rules_set` at `types.<id>.view.portrait` or
`set_manifest` ui.portrait defaults, and inspect the saved data before capturing
the selected subject. See docs/game-format.md and docs/view.md.

## Local selection from view scripts

Use `view.select({entityHandle, ...})` to replace the local selection or
`view.select_add(entityHandle)` to append. These are presentation requests,
not simulation commands. The client filters dead, hidden or unselectable
entities and clears command-card paging. Keep requests at most 256 handles;
pass generation-safe handles returned by `game.units`, not guessed IDs.

Author the file under `scripts/view/` with `script_write` and register it in
`viewScripts` through `rules_set`. Read it back with `script_read`; in Studio
use the existing Scripts panel. Trigger selection from the relevant view
event after the replacement entity exists. `demos/client-command-controls`
shows F7 selecting an armed tower; verify the selected portrait/command card
in the actual client. `view.selection()` queries the current local handles.
See `docs/ui-scripting.md`; this does not persist selection in game saves.
