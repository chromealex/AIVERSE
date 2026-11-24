# TerraWeave Caverns — Stage 27 handoff (2026-11-23)

## Repo / branch / commits
Repo `chromealex/AIVERSE`. Branch `feature/terraweave-caverns-core-20261123`. **NOT merged to main.**
- `11b1313e63c18210d6da9c7e5367774840aaa229`: created `engine/world/TerraWeaveCavernsVolumeCollision.h`.
- `14e037476e101017eb277571641c76eba011909c`: changed `engine/world/TerraWeaveCavernsMultiNav.h` to accept optional injected snapshot density callback.
- `6e470c9ab7290ffa043dcfeab9465afe20c50209`: created `engine/world/TerraWeaveCavernsAuthoredNav.h`.
- `4dc500a27d2af2989d391e5e73726723d40b7db5`: created `tests/terraweave_caverns_volume_collision_nav_test.cpp`.
- `1be428a3e9f0e2f734cd749eb7abda56fb0cf0cc`: added convenience `prepareCaveAuthoritativeTile` to build both mesh and collision from one authored snapshot.
- `baa7956519af89082109288d683623f986917006`: fixed expected unique vertex count in the synthetic collision test.
- `16eca2987cd267b3ddcc78cfda8292ef90865fdb`: explicit collision header standard includes.
- `508ccf9f4e46a75f50148a6503c680f366a9401e`: authored nav factory utility include.
- `78890e44729f9c19580e128b5e988e3d8e849f5b`: updated agent skill `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff is the subsequent commit, check actual HEAD for latest SHA.

## Real engine APIs consulted
- `engine/world/SpatialWorld.h`, `engine/world/TriangleCollision.cpp`: `SpatialWorld::validTriangle` rejects invalid/degenerate fixed-point triangles and enforces finite extent/edge constraints; `addTriangle` has an identical pre-validation path.
- `engine/physics/Scene.h`: `physics::BodyDesc` with `Motion::Static`, `ShapeKind::Mesh`, `vertices`, `indices`, `position`, `category`, `userData`.
- `engine/nav/NavMesh.h`: `nav::Source::spans/link` and `nav::Mesh::build/refresh`.
- Previous stage26 `CaveSpatialShadow`, `CaveBox3DShadow`, `CaveNavShadow`; Stage18 `CaveVolumeSnapshot`.

## Implementation and contracts
1. `prepareCaveVolumeCollision(mesh,resolution,spacingCm,revision,userData,limits)` converts chunk-local-metre indexed cave triangles into a `CaveCollisionPrepared` with both **absolute-world** fixed-point SpatialWorld triangles and a **chunk-local** Box3D static triangular mesh positioned at the signed X/Y/Z chunk world origin. It preserves the existing indexed winding across walls, floors, ceilings and overhangs. Deduplicates coincident local positions at integer-centimetre precision and enforces a 65,534-triangle / 65,535-vertex default Box3D mesh budget. Refuses bad indices, degenerates, nonfinite/out-of-chunk positions, invalid world extents and malformed SpatialWorld triangles.
2. `prepareCaveAuthoritativeTile(snapshot,key,resolution,spacingCm,...)` calls `generateCaveSnapshotMesh` **once** and uses the returned exact geometry for the physics payload, not a separately simplified/retessellated STRATA model. It is a CPU preparation convenience; does not create any live Box3D bodies or SpatialWorld colliders. A fully empty chunk currently fails collision preparation; zero-collider retirement needs an explicit API in a later stage.
3. The original `CaveNavSource` gains an optional `Density` callback. Omitted callback preserves existing base+entrances behavior. `makeCaveAuthoredNavSource(shared_ptr<const CaveVolumeSnapshot>,cfg,heightAtCm)` captures the same immutable full authored snapshot and delegates every sampled walkable span check to `caveSnapshotFieldCm`, so Carve/Fill operations now affect native nav source generation. Terrain height remains an owner-supplied callback.
4. New unit fixture checks floor/wall/ceiling triangle conversion, negative chunk origin, deduplicated mesh vertices, invalid index/degenerate geometry and budget refusal, plus native navigation showing walkable spans for a manually carved sphere and removing them after an overlapping Fill stamp.

## Files changed
- `engine/world/TerraWeaveCavernsVolumeCollision.h` (new)
- `engine/world/TerraWeaveCavernsMultiNav.h`
- `engine/world/TerraWeaveCavernsAuthoredNav.h` (new)
- `tests/terraweave_caverns_volume_collision_nav_test.cpp` (new)
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff.

## Tests actually executed
- GitHub connector acknowledged all stage27 create/update commits.
- **NOT RUN**: Stage27 C++ compiler, CMake/CTest, Box3D physics simulation, SpatialWorld cast/hit validation, NavMesh gameplay paths, native editor startup, STRATA upload/draw, Metal/Vulkan/GL GPU screenshot. New source test is **authored but unexecuted**. No complete AIVERSE checkout is available in the local runtime; do not confuse earlier stage24/25 isolated policy tests with this stage.
- Suggested first native step in a complete checkout: add `terraweave_caverns_volume_collision_nav_test.cpp` as a CMake executable linked to normal physics/NavMesh/SpatialWorld implementation targets, compile and run; then execute all Caverns stages 1–27 and check output. Verify with sanitizer and game collision tests before merge.

## Known limitations / next action
1. This conversion is **not collision publication**. Stage26 `CaveSpatialShadow::prepare` and `CaveBox3DShadow::prepare` can consume the new `CaveCollisionPrepared` type, but Box3D commit creates active body immediately and is still not a genuinely atomic multi-owner swap. Stage25 owner barrier is pure bookkeeping. A real host must pause physics readers, check world-wide ID leases, and stage/commit with rollback guarantees before acknowledging a brush change.
2. The generated `CaveMesh` may be open at chunk borders. Collision triangle orientation is inherited from the marching-tetrahedra mesh, but **closed manifold and correct one-sided Box3D mesh contact winding are not certified**. Audit border topology and actual Box3D collisions both directions, and keep authoritative collision sources aligned with versioned SDF snapshot.
3. Stage26 surface-cut geometry still needs to be combined or coordinated with underground cave-volume collision: avoid duplicated coplanar triangles and duplicate collision ownership.
4. A no-surface chunk (empty `indices`) cannot be prepared yet; implement safe retire/zero-collider path for filled cave removals and all-solid chunks.
5. Nav floor detection remains coarse vertical sampling and simple clearance checks, not a certified traversable 3D interior graph in every overhang. Add more tests for multi-floor caves, transitions, shaft walkability and agent height.
6. Next: wire these authoritative volume payloads to Scene owner during brush edits, coordinate full SpatialWorld/Box3D/NavMesh/GeometryScene visibility and retain previous state on failures. Fix missing true atomicity, add GPU-fenced retirement and CMake/CTest native compilation before any merge.
7. Keep mandatory per-stage skill update and independent dated handoff. Do not merge to main without real integration evidence.
