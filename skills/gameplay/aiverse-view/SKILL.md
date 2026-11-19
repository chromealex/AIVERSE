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

The old `game::RtsView` is a legacy compatibility adapter for
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
`modules/rts/view/RtsFog.h` and should not be imported
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

