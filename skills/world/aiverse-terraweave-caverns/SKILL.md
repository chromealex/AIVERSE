---
name: aiverse-terraweave-caverns
description: Generate deterministic genre-independent underground chambers, capsule tunnels, volume occupancy and smooth CPU cave meshes via world-space density and marching tetrahedra.
---

# TerraWeave Caverns — agent skill (stage 2)

Read `engine/world/TerraWeaveCaverns.h`, `engine/world/TerraWeaveCavernsMesh.h` and `docs/agent-handoffs/caverns-stage2-2026-11-23.md` before editing.

## API
- Namespace `aiverse::terraweave::caverns`.
- `Settings` stores a seed, cell size, tunnel/chamber sizes and probabilities, min/max Y. Positions and geometry parameters use **integer centimetres**.
- `isVoid(settings, PointCm)`: original binary analytic occupancy for block-world carving.
- `caveFieldCm(settings, PointCm)`: signed, approximate smooth density, **positive inside cave** and negative in rock. It is not exact Euclidean SDF.
- `generateChunk(settings, ChunkKey3, resolution, spacingCm)`: original X-fastest binary cell-center occupancy.
- `generateCaveMesh(settings, ChunkKey3, resolution, spacingCm)`: independent CPU surface geometry `CaveMesh` with triangle-list indices and chunk-local **metre** vertices/normals; place at world origin `key * resolution * spacingCm / 100` metres.
- Generated triangles are not welded; same-spacing neighbors sample the same world lattice, but different LODs require a seam transition system.

## Ownership and restrictions
- No dependencies on RTS/FPS or a renderer. CPU jobs create mesh; RHI uploads in a different layer; physics/navmesh should consume a durable mesh or bake.
- Do not duplicate/replace TerraWeave heightfield, VoxelGen or VoxelWorld. Opt-in adapters only.
- No persistent mesh cache or edits yet. Seed/settings + explicit generator version must be saved; **do not claim compatibility** after changing hash, surface extraction, or coordinate rounding.
- Field and mesh generation are compute-heavy; avoid updating per frame. Implement caching/budgets in a separate integration layer.
- Do not claim mesh is watertight or crack-free with arbitrary LOD differences without seam tests. No cave entrances, decorations, flora, cave lakes or collision in this stage.
- After every stage create a dated handoff describing exact changed files, APIs, actual tests, constraints, branch/commit and next action.

## Validation
Standalone tests:
`c++ -std=c++20 -O1 -I. tests/terraweave_caverns_mesh_test.cpp -o /tmp/cave_mesh_test && /tmp/cave_mesh_test`.
Run in checked-out repository, plus engine CI and negative-key/neighbor seam tests before merge.

## Stage 3: surface entrances (2026-11-23)
Read `engine/world/TerraWeaveCavernsEntrances.h` and
`docs/agent-handoffs/caverns-stage3-2026-11-23.md`.

- `EntranceSettings`: deterministic global X/Z lattice, radius, min/max search depth, search step and chance permille.
- `generateEntrances(caves, entrySettings, SurfaceRectCm, heightAtCm, excluded)`: template with callbacks for **authoritative surface height** and optional authored exclusion at entrance center; returns only candidates whose vertical ray finds an existing cave field greater than zero. Bounds are half-open. To cover a chunk and neighbor gradient samples, generate entrances from a larger surface rectangle than the target chunk.
- `Entrance`: stable identifier, signed-cm position of shaft, sampled terrain Y, verified existing cave Y, radius.
- `entranceFieldCm(entrance, point)`, `combinedFieldCm(caves, entries, point)`, `surfaceHoleAt(entries,x,z)`: union cave/shaft geometry and surface-hole footprint query.
- `generateCaveMeshWithEntrances(caves, entrances, chunkKey, resolution, spacingCm)`: meshes a combined field using `generateCaveMeshSampled`, while the old `generateCaveMesh` remains unchanged.
- To use TerraWeave: pass a lambda calling `aiverse::terraweave::heightAtCm(terrainSettings,x,z)`. Do **not** duplicate the height generator or mutate the terrain inside this pure library.
- **A surface hole is not yet applied to terrain meshes.** The owning TerraWeave surface adapter must clip/remove triangles within the footprint, regenerate collision/navmesh and invalidate appropriate chunks; never assume `surfaceHoleAt` modifies the heightfield automatically.
- A found cave connection is a pointwise density hit, not a proved traversable opening for a character; collision, minimum clearance and connectivity proof remain integration tasks.
- Persist cave, terrain and entrance seeds/config versions and authored overrides. Never treat generated entrances as mutable global singletons.
- Run standalone tests `tests/terraweave_caverns_entrances_test.cpp` in a real checkout before claiming verified functionality.

## Stage 4: cutting native TerraWeave terrain triangles
- Source: `engine/world/TerraWeaveCavernsSurfaceCut.h`. Handoff: `docs/agent-handoffs/caverns-stage4-2026-11-23.md`.
- `cutTerrainEntrances(const terraweave::TerrainPatch&, const std::vector<Entrance>&, sides=16, maxTriangles=200000)` accepts **authoritative** TerraWeave TerrainPatch (not camera LOD) and produces `CutSurface` with world-centimetre double XYZ triangles and dirty-region bounds.
- The cutter subtracts a regular polygon aperture per shaft from existing `TerrainPatch::collision` triangles, preserving winding and interpolating Y on each ORIGINAL terrain triangle plane. It uses a globally ordered id list and a strict triangle budget.
- For integration, **do not pass resulting variable-count triangle lists** to existing `TerrainCollisionPublisher::publish` or `makeEarthworksTerrainModel`: both expect fixed heightfield topology. Adapt the native publisher's topology contract so the same clipped triangle soup feeds both render::Model and SpatialWorld. Do not publish one without the other.
- `dirtyRegions` are merely planning metadata. They do not rebuild collision/navmesh/vegetation until the owner-thread publish barrier actually commits matching revisions. No new collision or navmesh owner has been implemented.
- Near aperture is an inscribed regular polygon; shaft field remains round. Ensure matching shaft aperture/transition rim in future stage to avoid polygonal collision lips.
- Missing: actual published runtime adapter, complete render+collision transaction, navmesh invalidation delivery, cave portal rim mesh, terrain material/UV seam, CTest registration, live GPU proof.

## Stage 5: native shared render/collision resident
- Read `engine/render/TerraWeaveCavernsResident.h` and `docs/agent-handoffs/caverns-stage5-2026-11-23.md`.
- `buildCutNativePayload(cut, cellSizeMetres, materialSource, uvScale)` converts **one** `CutSurface` triangle list to `CutNativePayload`: collision triangles in `Vec3/Fixed` and the matching `render::Model`; reject invalid geometry. `Model` uses same borrowed-texture policy as existing dynamic terrain.
- `CutTerrainResident::publish(device, spatial, std::move(payload), firstColliderId, cellSizeMetres)`: owner-thread opt-in publication. It uploads a new GPU model and adds its new collision range before removing old collision/model. **Caller MUST reserve a fresh, nonoverlapping global collider ID range** on each republish, and hide the replaced original terrain tile in BOTH render and collision owners. Cannot reuse the existing range on a republish.
- `CutTerrainResident::submit(renderer, origin)` follows existing Renderer3D model submission. `shutdown(device,spatial)` removes owned collision and releases GPU model before device destruction.
- `takeDirty()` yields candidate dirty rectangles **only after a successful publish**. At the host barrier, use these to invalidate existing native NavMesh, vegetation and Horizon derived state. This stage does not call native NavMesh APIs or guarantee navigable walk paths.
- No direct Box3D bridge yet. SpatialWorld triangle publications are real, but Box3D owners need a separate authoritative geometry replacement strategy.
- GPU model lifetime and publication failure paths need real Metal/GL/Vulkan and collider-ID exhaustion tests before main merge. The unmodified ordinary `TerrainPatchResident` remains owner of normal (unclipped) tiles.
