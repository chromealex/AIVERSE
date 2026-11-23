# AIVERSE World Processes — Stage 26: real Studio Scene authoring gizmos

Date: 2026-11-23. Branch `feature/world-processes-experimental`, PR #112 DRAFT. Main unchanged.
User requested feature work first, tests after completing functions.

## Implemented

- New `apps/studio-native/StudioWorldProcessesGizmos.h`, commit `722e97f5e189c3f761a9eca1546100a357957ea3`: bounded, read-only projection of **actual authored MapDoc DocEntity components** to the existing `StudioScene::OverlayShape` API. Uses stable authored entity IDs, never raw ECS indices or guessed world proximity. Shapes are ordinary native Scene lines, points, circles and labels; they render through the existing Renderer3D `Overlay2D` pass, not a separate WebGL renderer or custom ImGui tool.
- `apps/studio-native/StudioHost.cpp`, commit `3406a2d5dba621b8176eba59ce1cbe1059eb7ea4`: calls `worldprocess_gizmos::collect(session_->workspace().map)` inside the existing `StudioHost::rebuildOverlay` poll, alongside extension and spline gizmos, and forwards the merged shape vector to `StudioScene::setOverlayShapes`. No new native RPC, editor file format or modal UI.
- Visual conventions:
  - MORPH: cyan node markers, anchored node rings, spring/force connections.
  - CROWD: green agents, goal lines, slots and preferred-slot links. Authored coordinates are integer centimetres converted to scene metres; the map's actual object placement remains unchanged.
  - IMPRINT: orange trace cells sized by footprint+scent, diffusion links, emitters and tracker links.
  - CIRCUIT: yellow nodes, wire/gate inputs/outputs, switch/device connections.
  - GRANULAR: sand-colored 2D grid bounds using authored grid width/height, optional `DocEntity.extra.worldProcessGizmo.cellSizeMetres` (fallback 1 metre), label.
  - EVOLVE: blue region-marker circle using optional `DocEntity.extra.worldProcessGizmo.radiusMetres` (fallback 10m) and pressure link. **Not** authoritative native `EvolvePlantRegion` polygon coverage: the editor has no full region-authoring linkage yet, so the circle is explicitly just a visual helper.
  - SOCIETY: purple person markers, contact arrows/links, message and NPC trigger targets.
- Explicit shape budget max 1200 new shapes, bounded entity count, capped coordinates/radii and no modification of the persisted MapDoc or running World.
- This is the existing native Studio Scene viewport path shared by macOS and Windows hosts, not a fabricated screenshot.

## Architecture and caveats

- `StudioHost::rebuildOverlay` is already polled from normal Studio frame; it reads the real map and sends shapes to the same low-level Renderer3D overlay code that draws Lua extensions and spline editing.
- Existing Project design tokens remain unchanged (no new HTML/React panel). Gizmos do not own geometry, collider information, ECS runtime output or the Box3D scene.
- Editing is still through Scene Inspector/MapDoc. This stage adds previews, not handle picking/dragging or collision-aware 3D gizmo motion. Source links and shapes come from the authored document only; play mode's live ECS is inspected separately.
- No automatic conversion of integer MORPH/GRANULAR offsets to full mesh skinning weights; those remain Stage17 native host inputs.
- When `selectedId` is passed to the collector, it can filter to one authored entity, but the default host currently displays all available gizmos within budget.

## Test evidence

**None for Stage26 by explicit user instruction.** GitHub accepted file updates, but no GCC/Clang/CMake/Studio build, macOS Metal/Windows Vulkan viewport, screenshots, or full merged-tree Mac quick suite have been run. Do not label native Studio integration green.

## Next exact step

Build a valid example project overlay/scene from an EXISTING project's real game.json/rules.json and map using its valid spawned non-collider entity type; populate stable IDs and correct positions for all 10 world-process mechanisms. Mark demonstration as generated and not real native runtime-verified. Then complete Studio selection/editing of visual links and the actual standalone demo host before entering the user-approved consolidated test phase. Preserve concurrent `feature/studio-html5` work and update handoff per stage.
