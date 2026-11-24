# TerraWeave Caverns — Stage 18 handoff (2026-11-23)

## Provenance
Repository: `chromealex/AIVERSE`. Branch: `feature/terraweave-caverns-core-20261123` (not merged to main).
- `9786834c52fde6b79490da5e176057b46639ed3f` — new `engine/world/TerraWeaveCavernsVolumeSnapshot.h`.
- `d235f01342d303c4d206e788bda28c353fee72af` — new `engine/world/TerraWeaveCavernsSnapshotWorkers.h`.
- `b5c80d4d1d44209884cd79f9d4e8bb36567b6b23` — new `tests/terraweave_caverns_snapshot_workers_test.cpp`.
- `62ac4d689ab04e1c4f60775d406eb847cf3b4c40` — expanded `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff follows those commits; inspect feature branch HEAD for current SHA.

## Architecture / APIs
- `CaveVolumeSnapshot` combines base `Settings`, the full relevant `Entrance` catalogue, author-created sphere `CaveSphereStamp` instructions, generator format version and source revision. No ECS or renderer references.
- `canonicalizeCaveSnapshot(snapshot)` validates data and sorts entrance/stamp IDs. Maximum 16,384 stamps and 4,096 entrances. Duplicated stable IDs and invalid radii are rejected.
- `caveSnapshotFieldCm(snapshot,p)` unions analytic cave density, entrance shafts and Carve spheres; Fill spheres subtract material with deterministic priority independent of incoming stamp order. Positive density is cave void; negative is solid. It is an approximate integer field, not an exact Euclidean SDF.
- `generateCaveSnapshotMesh` connects the unified immutable field to the existing `generateCaveMeshSampled` output (chunk-local metres).
- `CaveSnapshotRegenerator` accepts `shared_ptr<const CaveVolumeSnapshot>`, checks request.sourceRevision, launches bounded CPU-only `std::async` mesh jobs, checks responses with Stage16 `CaveSeamPublicationGate::accept`, then calls `gate.analyze()` after accepted work. Stale or failed jobs cannot publish geometry.
- Regression source validates canonicalization, fill-over-carve, deterministic mesh, duplicate stamps and one asynchronous seam job.

## Relation to existing engines
- TerraWeave still owns terrain; STRATA owns presentation-side geometry clusters/LOD and GeometryScene draws. Box3D, SpatialWorld and nav::Mesh remain independent authoritative owners.
- Inspected `engine/physis/PhysisParallel.h` and `engine/animation/MotionWeaveWorkers.h`: both provide persistent pools but their worker interfaces are synchronous barriers, unsuitable for nonblocking per-frame completion without host-owned task queue. Stage18 retains a bounded `std::async` adapter; it does NOT claim integration with a common AIVERSE job system.
- Persist source settings, generator version, entrance/stamp stable IDs and sourceRevision with authored world state. Stage18 implements memory representation and generation only — no actual save file schema, undo/redo, editor brush or replication.

## Tests actually executed
- GitHub connector successfully wrote new code, test source and skill updates.
- **Local baseline (not Stage18):** from the previously available partial `/mnt/data/caverns_stage2` harness, `c++ -std=c++20 -O1 -I. tests/terraweave_caverns_mesh_test.cpp -o /tmp/caverns-stage2-local-baseline && /tmp/caverns-stage2-local-baseline` completed successfully; stdout: `caverns mesh OK triangles=3252`.
- **Stage18 tests NOT RUN:** full AIVERSE repo could not be checked out into the local container because github.com DNS lookup failed. Stage18 header compile, dedicated worker test, CMake/CTest, full engine, editor, Box3D, GPU or STRATA acceptance have not run. Do not promote the baseline result to a Stage18 validation claim.
- Suggested full-checkout command: `c++ -std=c++20 -O1 -pthread -I. tests/terraweave_caverns_snapshot_workers_test.cpp -o /tmp/caverns-stage18 && /tmp/caverns-stage18`; run CMake/CTest and sanitizers after compiling.

## Open issues / next stage
1. Integrate immutable snapshot catalogue and authored brushes into editor, with undo/redo and explicit save/replay schema. All neighboring chunks must use the same complete influence halo to prevent seams.
2. Current Stage18 worker uses `std::async`, not PHYSIS/animation shared job infrastructure. Design a persistent asynchronous worker queue with bounded per-frame dispatch, cancellation and explicit GPU/physics owner thread commit.
3. Stage16 gate can fail closed when equal-resolution procedural borders disagree. Audit actual generated chunk pairs and repair field/mesh mismatches before allowing STRATA publication.
4. Actual `GeometryScene`/STRATA, SpatialWorld, Box3D and NavMesh atomic commit, retirement fences and rollback remain missing. Worker acceptance is only CPU preparation.
5. Explicitly version serialization and metadata fingerprint for all settings/stamps/entrances. Current validation sorts by IDs, but does not compute a cross-platform save hash or implement persistence.
6. Keep skill and dedicated dated handoff after every stage. No main merge before real compiler/CTest and integration evidence.

## Post-handoff source correction
- `c2e73b39c660734d7a564eb11aeff03be4504aa7`: repaired another literal escaped newline between `#include <cstdint>` and `#include <array>` in `TerraWeaveCavernsSeamGate.h`. This is a source fix, not proof of compilation; Stage18 C++ tests remain NOT RUN.
