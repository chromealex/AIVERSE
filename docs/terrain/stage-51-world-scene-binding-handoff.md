# TerraWeave Stage 51 — immutable WorldScene soil and resident replacement tiles

Date: 2026-11-19. Branch: `feature/terraweave`; PR #24 remains Draft.

## Actual implementation commits

- `4328f79f44461cc2e581e6f05f2c4fbcaef92bef`: `engine/world/WorldScene.h` imports Earthworks, adds `setVegetationTerrainSnapshot(shared_ptr<const EarthworksLayer>,vector<RectCm> edited)`, `vegetationTerrainRevision()` and immutable presentation state.
- `dab617883535d590ad9e102aaa0bd75c42b2e0e2`: `engine/world/WorldScene.cpp` accepts the owner-thread snapshot, validates max 128 affected rectangles, resamples resident authored height tile vertices from immutable Earthworks using matching Fixed X/Z grid positions, and captures the SAME shared snapshot in near-forest and distant Horizon height callbacks. Unloaded far regions can also sample Earthworks before the procedural fallback. Horizon clips/near-seam normals still use the resident authored tile snapshot; NEREID water surfaces, cascades and PHYSIS remain unchanged.
- `8b8fa59278ea2abd6881bb2f8a5d69ee770176f6` and `0848b80460e816f32edf8c94da87d9a67b83ff0a`: `WorldScene::setReplacedTerrainTiles(vector<world::Cell>)` suppresses ONLY original terrain model #0 in specified resident cells, without dropping other scene models or water. Native host must pass *only* already-uploaded Earthworks tile replacements; collision still lives outside WorldScene.
- `ea9c52d429d2c94ffa0354f40f5e0e8006ee7656`: public `WorldScene::worldCellSizeMetres()` to catch mismatched external terrain tile size.
- `b3decd4a5c4a55a8f21df0d00a093fc7ee523394`: `TerrainMutationCoordinator::presentationSnapshot()`, `residentCells()`, `terrainCellSizeMetres()` and non-destructive `pendingTerrainNotifications()` for native scene integration; snapshot deep-copies Earthworks' sparse mutable edit map, but its BaseSampler closure MUST capture immutable underlying terrain data.
- `915ce51e6083c7c5159b4f9fa7aa3c121fbf627f`: NEW `engine/world/TerraWeaveWorldSceneBinding.h` with `WorldSceneEarthworksBinding::synchronize(WorldScene&,TerrainMutationCoordinator&)` and `reset(WorldScene&)`. Binder hides original static terrain draws as soon as corresponding replacement GPU tiles are resident, but only pushes the new near/far immutable soil snapshot after all requested pinned dynamic terrain tiles are ready. Uses bounded dirty rectangles for precise Horizon invalidation, falls back to full-source rebake for >128 changes.

## Host integration contract (feature provided, NOT automatically installed)

Create a `TerrainMutationCoordinator` with authoritative Earthworks/RootSupport/GrassCover registries and shared stream cell size/resolution. Consume completed NEREID bank probes (flow/soil) or local `excavate(brush,tick)` on the authoritative update barrier. On owning GPU/collision thread call `publishVisible(device, spatial, pinnedCells,uploadBudget,colliderIdForCell,materialSource)`; the native game must reserve unique collider IDs, not leave duplicate old static terrain colliders and must pin collision tiles around simulation actors (not just visible camera tiles). Then call `WorldSceneEarthworksBinding::synchronize(worldScene, coordinator)`, before `worldScene.update` and `worldScene.draw`; after `worldScene.draw`, call `coordinator.draw(renderer,renderOriginCm)` to draw its actual GPU replacement and batched grass. The old static terrain model draw is skipped only where the dynamic resident model is ready. Use `coordinator.refreshNavMesh(existingNavMesh,EarthworksNavSource)` after collision publication, and notify other consumers of `takeTerrainDirty` after all have consumed the same revisions. Shutdown: `binding.reset(worldScene)`, then `coordinator.shutdown(device,spatial)`, then `worldScene.shutdown(device)` while the gfx Device is alive.

### Native host pseudo-code (API names are real; ownership data are supplied by game)

```cpp
// After fixed ECS tick:
auto change = terrain.excavate(brush, authoritativeTick);
// Or terrain.erode(nereidBankProbes, authoritativeTick, timestepMs)
// Render/collision owner phase:
terrain.publishVisible(device, spatial, pinnedCells, 2, colliderIdForCell);
binding.synchronize(worldScene, terrain);
worldScene.update(device, observers);
terrain.refreshNavMesh(navmesh, navSource, &err);
// After renderer.beginScene():
worldScene.draw(renderer, renderOriginCell, worldCellSize);
terrain.draw(renderer, renderOriginCm);
```

The read-only forest/Horizon snapshot does **not** itself regenerate static terrain vertex buffers or native Box3D mesh bodies. Only `TerrainPatchResident` and the optional `TerrainBox3DPublisher` do that. Physics authority should replace static Box3D tile bodies between `physics::Scene::step()` calls; falling tree bodies via `FallenPlantPhysics::spawn` are server-owned and replicated (Box3D float physics is not cross-platform lockstep).

## Status, limitations and next actions

**Source writes and GitHub readbacks verified. NOT RUN:** actual CMake, Clang/GCC/MSVC, CTest, editor/RTS/FPS client execution, Box3D motion, NavMesh dirty-path tests, Metal/GL/Vulkan screenshots, performance and memory profiling. Previous 9/9 TerraWeave CPU and 1/1 timing router pass are earlier revisions only; do not claim new features passed. No native game calls the WorldScene binder automatically yet; this is an opt-in integration API, and the physical server/renderer must obey publish/read barriers and static-terrain ownership. Near terrain source uses a heightfield (cannot represent actual caves/undercuts). Borrowed authored material textures must remain resident while the dynamic tile uses them. Avoid capturing mutable ECS/WorldScene objects in Earthworks BaseSampler; copying its `std::function` does not freeze mutable captures.

**Next:** add actual native project/example wiring using the project's collision/stream/persistence rules and check tests, remove typo in pseudo code if copied; implement falling tree navmesh obstacle invalidation and ECS persistence/replication adapter. Keep PR Draft until integrated QA proves parity.
