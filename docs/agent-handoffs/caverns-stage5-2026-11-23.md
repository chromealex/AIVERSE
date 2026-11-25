# TerraWeave Caverns — stage 5 native render/collision adapter (2026-11-23)

## Repository and commits
- Repository: `chromealex/AIVERSE`.
- Working branch: `feature/terraweave-caverns-core-20261123` (originally based on main, not merged).
- `6233576c079374677a208519f1043287ae3f9b7b`: `engine/render/TerraWeaveCavernsResident.h`.
- `e92e4e29d636c3cee08bcbcab70fb845ce269184`: `tests/terraweave_caverns_resident_test.cpp`.
- `4c6907f85eaebfe57f09b589735fb2284dcd573d`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- Handoff commit follows these changes.

## Architecture and API
- Existing TerraWeave standard path keeps `engine/world/TerraWeaveTerrainPatch.h` and `engine/render/TerraWeaveTerrainPatchRender.h` unchanged. Those contracts require rectangular heightfield topology and cannot ingest variable-count clipped triangles.
- The new, **opt-in** `engine/render/TerraWeaveCavernsResident.h` consumes stage-4 `CutSurface` and creates one shared `CutNativePayload`, with `Fixed` world triangle collision and render::Model vertices derived from the same quantized collision triangle values. The render model uses chunk-local metres; runtime draw uses normal TerraWeave cell transform.
- `buildCutNativePayload(cut,cellSizeMetres,materialSource,uvScale)` validates dimensions, triangle count and `SpatialWorld::validTriangle`, constructs a dedicated nonpooled model (single primitive), optionally copies borrowed material textures.
- `CutTerrainResident::publish(device, spatial, std::move(payload), freshColliderId, cellSizeMetres)`: staged upload and collision replacement. Requires unique fresh collider IDs not occupied by other tiles or actors; no overlapping old/new ID ranges. Adds all new colliders before removing old; on insertion failure removes added and releases candidate GPU resources. Uses graphics owner device idle check for replacing old model. Generates dirty records only on successful swap.
- `CutTerrainResident::submit(renderer, origin)` and `shutdown(device, spatial)` integrate with shared Renderer3D/SpatialWorld ownership.
- `takeDirty()` returns candidate affected surface bounds after successful publication; host must route into the **existing** navmesh/vegetation/terrain dirty consumers at its owner barrier.

## Actual validation
- Code, regression test, skill and handoff created through GitHub connector.
- **No compiler run / CTest / GPU or live SpatialWorld acceptance was executed in this stage.**
- New regression test only exercises CPU buildCutNativePayload shape and static validation; it does not exercise live gfx::Device, runtime publish or navigation.
- Suggested local compile command: `c++ -std=c++20 -O1 -I. tests/terraweave_caverns_resident_test.cpp -o /tmp/aiverse-caverns-resident && /tmp/aiverse-caverns-resident`. Full CMake/C++ integration may need additional engine object linkage.
- Do not mark the resident production-ready or merged.

## Limitations / critical handoff tasks
1. Run real compilation immediately and fix any engine type/API mismatches. Register CMake/CTest for stage 1–5.
2. Live proof: simultaneously hide legacy tile draw and remove original TerrainCollisionPublisher range. Failure to do both causes duplicate ground, blocking holes or ghost collision. Coordination must remain at a single graphics/collision barrier.
3. This publication requires globally assigned **new nonoverlapping** collider IDs on every revision and cannot reuse an existing ID range. Build a bounded per-tile reservation service before production.
4. Dirty regions are delivered after successful commit, but **native NavMesh, Box3D, vegetation, Horizon and VoxelGen are NOT wired**. The host must translate rectangles to existing NavMesh invalidation/refresh at an authoritative tick boundary and ensure physics/collider lifetime parity.
5. No terrain-to-shaft wall/rim mesh or proven walkable ingress. Entrance aperture approximates polygon while shaft remains radial; annulus could obstruct character.
6. No pre-registered scene adapter, editor integration, GPU screenshots, LOD transition or complete build/replay certification.
7. Retain CPU authority; GPU presentation isn't simulation. Keep every stage documented in its own handoff and update the skill.
