# TerraWeave Stage 47 — coherent terrain render + collision publication

Date: 2026-11-19. Branch: `feature/terraweave`, Draft PR #24.

## Implemented source

- `3c073d2fdb29671145f121e18ddf8f977712ac6e`: new `engine/world/TerraWeaveTerrainPatch.h`.
- `e56b5978f0617932baa69ca68ffc66a60cf6507e`: new `engine/render/TerraWeaveTerrainPatchRender.h`.
- `a94cbaa38fc9a4cb7e156651d5f9f990ad7f07b9`: preallocation fix for dirty-region queue before collision transaction.

Public API: `bakeEarthworksTerrainPatch(EarthworksLayer&,world::Cell,cellSizeMetres,resolution)` builds a tile with the exact `readChunkCollision` diagonal, signed Fixed X/Z, bounded terrain heights, 2 triangles per quad and a source Earthworks revision. Each triangle feeds both the existing `world::SpatialWorld` collision and the existing `render::Model` builder; no separate ground triangulation path.

`TerrainCollisionPublisher::publish(SpatialWorld&,patch,firstColliderId)` reserves a unique ID interval per tile, refuses to steal non-terrain collider IDs, stages all changes on a copy of `SpatialWorld`, and commits only if every triangle is accepted. `unload` removes only owned IDs. Caller MUST reserve distinct collider ranges and ensure the pre-existing/static terrain collider is not simultaneously active. This is a native C++ opt-in adapter, not automatically installed into FPS/RTS authority.

`makeEarthworksTerrainModel(patch,authoredMaterialSource?,uvScale)` uses the existing `render::Model`/RHI material path and exactly the same triangles. Without a source material it uses a simple opaque soil fallback. With a source material it borrows its uploaded textures, requiring that the source remains GPU resident until the dynamic tile is retired. `TerrainPatchResident::publish(device,spatial,collisions,patch,id,source)` prepares and uploads the next mesh on the Device owner thread, then commits the new collision transaction and swaps the visible GPU model; failure preserves the previous tile. `submit(renderer,originCm)` uses the existing Renderer3D; `takeDirty()` provides bounded per-tile source revision/rectangles for explicit navmesh, near-forest, Horizon and scene invalidation by the native host. `shutdown` frees the model and owned collider IDs explicitly on the owner thread.

## Status and important limits

**Source committed via GitHub. C++ builds, CTest, actual Render3D GPU screenshots, FPS/RTS collision integration, Metal/GL/Vulkan, and performance are NOT RUN** (features-first requested; Mac QA later). Prior green TerraWeave tests belong to earlier revisions.

At this stage, native game code must explicitly call the patch adapter and replace its old tile terrain draw/collision set. Navmesh only receives a dirty-region contract; its actual rebuild must be wired by the authoritative host. The adapter is a HEIGHTFIELD: it cannot represent caves or hollow overhangs. Large tile rebuilds currently replace the complete tile; no sparse VB patch or GPU grass instancing here. Actual falling-tree Box3D body spawning is still distinct from the already available deterministic `PlantFallEvent`.

## Next implementation stage

Create a shared native runtime coordinator that owns an EarthworksLayer/NEREID bridge, marks all edited tile-neighbor patches dirty, schedules at most a configurable count of render/collision publications per frame, exposes separate navmesh/vegetation notification queues, and consumes sparse `GrassCoverRegistry` and `RootSupportRegistry` results at the same authoritative tick. Keep the existing `WorldScene` and NEREID ownership of water unchanged, and avoid editing concurrent agents' forest/scene files without rereading latest head.

If compilation fails, record the very first Clang/GCC error and fix source before claiming test pass. Keep PR Draft.
