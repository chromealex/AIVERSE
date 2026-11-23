---
name: aiverse-terraweave-caverns
description: Generate genre-independent deterministic underground 3D void volumes, chambers and tunnels by world-space integer coordinate queries and chunk sampling. TerraWeave subterranean geometry foundation.
---

# TerraWeave Caverns

Read `engine/world/TerraWeaveCaverns.h` and `docs/agent-handoffs/caverns-stage1-2026-11-23.md`.

Use `aiverse::terraweave::caverns::Settings`, `isVoid(settings, PointCm)` and `generateChunk(settings, ChunkKey3, resolution, spacingCm)`. All positions/sizes are integer **centimetres**. `isVoid` is authoritative for cave emptiness within the allowed vertical band. Sample output has X-fastest cell-center occupancy (1=air).

Do not replace `VoxelGen`, `VoxelWorld`, TerraWeave heightfield or renderer with this module. Integrate via opt-in bridges: voxels can carve air using `isVoid` where a block is solid, volumetric SDF meshes can sample the same world function, collision/navmesh is built from generated geometry. Respect authored edits and game save revisions.

No RTS/FPS includes or global RNG. Generator version must be persisted alongside settings and seed. Runtime worker generation and ECS commit/render upload are separate stages. Do not imply this header performs smooth meshing, collision, fluid simulation, entrances or editing.

After each stage write its own handoff with exact changed files, APIs, tests actually executed, limitations, branch/commit and next action.
