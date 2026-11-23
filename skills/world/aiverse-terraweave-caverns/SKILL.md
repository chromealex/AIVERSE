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
