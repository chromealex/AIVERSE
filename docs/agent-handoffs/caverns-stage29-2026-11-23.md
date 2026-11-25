# TerraWeave Caverns — Stage 29 handoff (2026-11-23)

## Repository and provenance

Repo: `chromealex/AIVERSE`; branch: `feature/terraweave-caverns-core-20261123`, not merged into main.

Stage29 commits:
- `9ea78196548e684a0ddc4247664c201736334f9c`: `engine/render/TerraWeaveCavernsShardCatalog.h` — authoritative, revisioned full-shard preview/catalog for mixed empty, edited, unchanged source chunks
- `0e7c602dbdcfee436bcb7511c446db8b40c6932c`: `engine/world/TerraWeaveCavernsAuthoredNav.h` — fix unspecified evaluation order of shared_ptr move relative to settings/entrances reads
- `e813c23fb757551072afff6071e52256ec46a907`: `engine/world/TerraWeaveCavernsAuthoredNavStage.h` — prepare/commit/abort native NavMesh with same immutable SDF snapshot
- `4e4a7608e5ead476fab54353b3a499d9d4cf244d`: `tests/terraweave_caverns_mixed_shard_test.cpp`
- `aa2d4eb853f84239d9a1eddc640d54925f56a4ac`: `tests/terraweave_caverns_authored_nav_stage_test.cpp`
- `9836c940acd3a6044417083798cf739c99640edc`: mandatory update to `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This dated handoff is a subsequent commit; inspect HEAD before continuing.

## Implementation

**Full mixed-shard CPU presentation catalog:** `CaveShardCatalog` owns committed `CaveBatchChunk` source slots, including tombstones for all-empty cave chunks. `preview(shard,edits,batchRevision)` produces a deterministic complete `CaveShardSnapshot` composed of unchanged plus changed entries and explicitly empty entries, without changing committed state. It rejects stale revisions, duplicated edit keys and invalid mixed-shard input. `adoptSnapshot` is to be called only AFTER the external GPU+physics+nav host commit. `prepareFullCaveShardPresentation(snapshot,spec)` delegates to Stage11 `prepareCaveBatch` and supplies a complete source list to one `GeometryScene`, skipping only empty geometry. If all sources are empty, return empty CPU staging for `CaveGeometrySwapOwner::stageEmpty`. This avoids the Stage28 bug pattern where a partial list of only edited chunks would accidentally delete unchanged resident models on full-scene replacement.

**Native authored NavMesh staging:** `CaveAuthoredNavStage` captures a matching canonical `shared_ptr<const CaveVolumeSnapshot>` and uses the Stage27 authored `CaveNavSource` to prepare an independent `nav::Mesh` through Stage26 `CaveNavShadow`. Active nav is retained during staging. `commit(revision)` swaps only matching revision, and `abort(revision)` preserves the active nav. The original nav factory was corrected to copy settings/entrances BEFORE moving shared snapshot to the callback, avoiding unspecified C++ argument evaluation order and potential moved-from dereference.

**Test sources:** `terraweave_caverns_mixed_shard_test.cpp` checks three signed-negative neighbor entries (unchanged, emptied, updated), full two-model GeometryScene manifest after one source retirement, stale refusal, catalog commit and later all-empty stage. `terraweave_caverns_authored_nav_stage_test.cpp` checks a canonical Carve snapshot staged without exposing the nav mesh, version mismatch, successful commit, Fill snapshot staging and abort, then a newer Fill snapshot commit.

## Files changed

- `engine/render/TerraWeaveCavernsShardCatalog.h` (new)
- `engine/world/TerraWeaveCavernsAuthoredNav.h` (fix)
- `engine/world/TerraWeaveCavernsAuthoredNavStage.h` (new)
- `tests/terraweave_caverns_mixed_shard_test.cpp` (new)
- `tests/terraweave_caverns_authored_nav_stage_test.cpp` (new)
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff document.

## Verification actually performed

- GitHub connector confirmed successful source and documentation commits.
- **NOT RUN:** full native AIVERSE C++ compilation and CMake/CTest, new Stage29 regression executables, GPU/STRATA geometry swap, Box3D collision, SpatialWorld authoring after fill, NavMesh gameplay simulation, Editor Scene UX or multi-backend captures. The new tests are source only and must not be described as PASS.
- Suggested first validation in a complete checkout: register these two test executables in the project's existing CMake/CTest setup and run them linked to normal GeometryScene/Box3D/NavMesh targets, then all Caverns stages1–29, followed by native Editor/STRATA/physics integration and sanitizer acceptance.

## Known limitations and exact next step

1. This catalog is CPU-only. It does not yet own actual `CaveGeometrySwapOwner` resident GPU scenes, nor does `CaveEditRuntimeBridge` call `preview` or adopt the snapshot after a real successful host commit. Connect these only at the renderer + simulation-owner barrier, with explicit no-duplicate-draw rules.
2. Batch size and GPU lifetime: each affected shard is re-uploaded as a whole. Stage24 `GeometryScene` STRATA sidecars still require device-idle confirmation for certain retirements; nonblocking fence retirement is NOT solved. An all-empty source may require only visual retirement, not a new scene.
3. Stage26 Box3D creates an active body immediately on commit and cannot provide a true invisible-body prepare/commit/rollback transaction yet. Stage25 is only owner readiness bookkeeping. Do not confirm `CaveEditRuntimeBridge::publishReady` when physics owners are only CPU-staged.
4. NavMesh spans use approximate vertically sampled SDF clearance; actual gameplay and agent-height path correctness have not been validated.
5. Catalog's `adoptSnapshot` assumes caller's external barrier has frozen competing source writes; it does not itself make GPU resources atomic. Hosts must handle late arriving edits and stale catalogue states.
6. A complete authoring flow still needs global collider ID leases, actual physics and graphics owner wiring and restart/rollback if one subsystem fails. A mixed-shard source snapshot cannot substitute for collision/navmesh synchronization.
7. **Priority:** compile/test the full feature branch, not just one self-contained policy header. Fix CMake and compiler errors, run actual GPU/Box3D/NavMesh acceptance, and only then consider merging to main.
8. As always, update `skills/world/aiverse-terraweave-caverns/SKILL.md` and write a new dated `docs/agent-handoffs/caverns-stageN-YYYY-MM-DD.md` after the next stage.
