# TerraWeave Caverns — Stage 26 handoff (2026-11-23)

## Repository / provenance
Repository: `chromealex/AIVERSE`; branch `feature/terraweave-caverns-core-20261123`, **not merged into main**.
- `938ba8e6f9b4d077c9432eb3f2afe170123e0aa0`: new `engine/world/TerraWeaveCavernsShadowOwners.h`, CPU-only cut-surface collision preparation and independent native NavMesh shadow.
- `853ebc2dfdd96b045e8b1ffad1ced32f31ec0880`: new `engine/world/TerraWeaveCavernsSpatialShadow.h`, SpatialWorld collider lease and rollback on failed insertion.
- `2e474021423039dd574ea26f8e4ee2bb161c2ab0`: new `engine/world/TerraWeaveCavernsBox3DShadow.h`, prepare-only Box3D body descriptors.
- `17f6959e4fbf6e7527d67517628bc03b495f3993`: new `tests/terraweave_caverns_shadow_owners_test.cpp`.
- `adf18ab5d54b5ba9321ba1f3920e2292ce333653`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff file is committed next; verify current HEAD before modifying.

## Engine interfaces inspected
- `engine/world/SpatialWorld.h` and `SpatialWorld.cpp`: collider insertion via `addTriangle(id,triangle)`, `remove(id)`, `validTriangle`, `colliders()`. No transaction API or invisible staged colliders exists.
- `engine/physics/Scene.h`: `Scene::create(BodyDesc)` immediately creates a live Box3D body; `Scene::destroy(BodyHandle)` destroys it. No prepare-only native shape and atomic multi-owner swaps.
- `engine/nav/NavMesh.h`: genre-independent `nav::Mesh::build(source,config,error)` and mutable `refresh`, enabling a separate shadow `nav::Mesh` before pointer handoff.
- `engine/world/TerraWeaveCavernsBox3D.h` and `engine/render/TerraWeaveCavernsResident.h`: existing immediate publishers and surface-cut triangle payload are not suitable as reversible staging of entire underground cave collision.

## New API
1. `prepareCaveCollision(CutNativePayload,tileMetres,userData)` constructs `CaveCollisionPrepared` with one revision, native `world::Cell`, immutable copied triangles, and matching Box3D `BodyDesc`. This stages only CPU data, not live physics or a full cave wall mesh.
2. `CaveNavShadow::prepare(nav::Source,cfg,revision)` creates a completely separate native `nav::Mesh`; `commit(revision)` swaps it into the active pointer; `abort(revision)` discards the candidate. Host must pause readers for swap and keep native nav source version-aligned with visual and collision edits.
3. `CaveSpatialShadow::prepare(CaveCollisionPrepared,firstId,SpatialWorld)` verifies globally unoccupied dedicated ID range, source revision and triangle validity. `commit(SpatialWorld,revision)` inserts new colliders first, removes inserted IDs on failure, only then removes old active range. `abort` drops staged CPU triangles without touching live collision. External collider ID allocations must not race the commit.
4. `CaveBox3DShadow::prepare(CaveCollisionPrepared)` stages CPU `BodyDesc`; `commit(Scene,revision)` creates a live body at a non-simulating owner barrier and then destroys previous body, leaving old state in place on a create failure. `abort` drops descriptor. **New and old Box3D bodies can briefly overlap during the call; this is not an atomic inactive-shape swap.**

## Test coverage and actual execution
- Authored `tests/terraweave_caverns_shadow_owners_test.cpp` to exercise no-live-mutation before prepare, SpatialWorld successful commit and abort, native NavMesh shadow preparation/revision gating and abort, and Box3D descriptor prepare/abort.
- GitHub connector acknowledged file/skill commits.
- **NOT RUN:** complete AIVERSE build, test compilation/linking, CTest, actual Box3D Scene commit simulation, SpatialWorld failure-injection rollback, STRATA GPU, native editor/UI, physics/nav gameplay or GPU acceptance. No stage26 success claim beyond source commits is justified.
- A suggested first focused native test in full checkout is to register this file as a CMake test target linked with `engine/world/SpatialWorld.cpp`, Box3D library, and any normal AIVERSE nav/physics dependencies. Do not assume a single translation-unit compile links all symbols.

## Current limits and next concrete stage
- True four-owner atomicity is still **NOT IMPLEMENTED**. Stage25 owner barrier is bookkeeping. Stage26 introduced individual preparation protocols, but Box3D has no inactive body API; multiple owner commits can still partially fail. Do not call `CaveEditedSceneTransaction::commit` with a dummy callback or label overall swap atomic without a verified host all-or-none barrier.
- Physics geometry prepared here refers to TerraWeave entrance **surface-cut triangles**; authored SDF cave volume walls are still missing authoritative collision conversion and Box3D/SpatialWorld/navmesh parity.
- `CaveNavSource` Stage7 samples the original cave field plus entrances, not authored fill/carve. A new nav source must use `caveSnapshotFieldCm` for actual edit parity.
- Pre-reserve globally unique collider ID ranges, bound cache lifetimes, and pause simulation/queries at the owner barrier; no concurrent reads during SpatialWorld or NavMesh commit.
- Next: introduce full cave-volume `CaveMesh` authoritative collision conversion, authored-volume nav source, and a genuinely rollbackable physics swap, ideally via inactive Box3D bodies or shadow physics scene. Then connect Stage23 editor, Stage24 GPU staged scene, Stage25 owner ticket to a real host-controlled editor/gameplay commit point.
- Register and run full Caverns Stages1–26 CMake/CTest, test Metal/GLES/Vulkan / Box3D and nav gameplay before merging to main; maintain the skill and a dedicated handoff after every stage.
