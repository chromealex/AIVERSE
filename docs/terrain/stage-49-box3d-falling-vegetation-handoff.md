# TerraWeave Stage 49 — optional Box3D terrain and fallen plant physics

2026-11-19, `feature/terraweave` (Draft PR #24).

## Source and commits

- `8aec80ab0670f293b7004fbb30a69c05c8f77c19` — `engine/world/TerraWeaveBox3DTerrain.h`: `makeTerrainBox3DBody` deduplicates the *exact* `TerrainPatch::collision` world vertices into a local static Box3D mesh with the same triangles/winding as the RHI model and SpatialWorld collider. `TerrainBox3DPublisher` creates a new static tile body before destroying the old one, stores versioned generation-safe handles and rejects over-budget residency. Must be called on the owning `physics::Scene` thread between `step()` calls; optional native project must link `AIVERSE::physics` and configure Box3D.
- `438441edfb3666cbde68d08438b5b35710c9ee5d` — `engine/world/TerraWeaveFallenPhysics.h`: `FallenPlantPhysics::spawn(PlantFallEvent,FallenPlantBodySpec,TerraWeaveForestScene*)` creates ONE dynamic Box3D capsule per unique tree/shrub ID, initial lean/twist directed downhill toward washed-out soil, and optionally hides the upright forest instance through `removeTree`. Stale duplicate event cannot spawn a second body. `submit(renderer,origin,prototypeBySpecies)` uses existing Arbor model with Box3D pose, no replacement VFX shader; `erase()/shutdown()` free bodies and permit explicit replant. Species dimensions and physical materials are caller-configurable. `snapshot()` exports identity/handle/tick metadata; it is not a portable physics save format.
- `cf383dfb0b39b1c9f0f530eb9da9877f1c312c27` — isolated fall render-origin POD and bounded world-scale radius/height checks.

## Ownership and workflow

Terrain native host owns `EarthworksLayer` + root and grass registries + NEREID ECS water snapshots. A completed bank/wave erosion tick or dig action produces `EarthworkChange` and once-only `PlantFallEvent`. At an authoritative physics barrier, native game replaces old Box3D static tile geometry using the new `TerrainPatch`, then converts fall events into dynamic capsule bodies through `FallenPlantPhysics`. The game must replicate body transforms to clients and either rebuild navmesh obstacles or listen to `TerrainPatchDirtyRegion`; use its own server persistence/replay. The optional Box3D layer is not a replacement for deterministic `SpatialWorld`, which still supports FPS hitscan and generic collision from the same triangles. Different host threads must pass immutable snapshots at a barrier; no direct async sharing of mutable Earthworks or Box3D Scene.

**Important limitations:** Dynamic physics pose is not cross-CPU deterministic lockstep (Box3D is float); host server must own and replicate. There is no implicit Box3D `Scene::step` call from this adapter and no automatic installation in games. Fallen tree collider is approximated as a capsule; exact articulated branches, trunk-break mechanics, full root pullout rigid geometry and per-branch collision are not implemented here. HLOD may need per-tree exclusion if the original standing tree remains in far forest clusters. The game should not submit the original standing model after a successful fall event. Original static terrain mesh/collider must be retired rather than rendered alongside a dynamic replacement.

## Validation status / next

Source committed through GitHub. No new C++ compile, CTest, Box3D native runtime, Metal/GL/Vulkan screenshots, collision/navmesh tests or perf benchmark were run. Previous 9/9 TerraWeave tests and 1/1 GPU timing router pass refer to earlier head only. Do not claim this optional integration operates in a running RTS/FPS scene yet. Keep PR #24 Draft. Next: wire safe native client/WorldScene entry points and explicit dirties into actual navmesh/physics world update phases, add test coverage and visual QA after all features.
