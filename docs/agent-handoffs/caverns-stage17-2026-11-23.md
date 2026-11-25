# TerraWeave Caverns — Stage 17 handoff (2026-11-23)

## Branch and provenance
- Repository: `chromealex/AIVERSE`; work branch `feature/terraweave-caverns-core-20261123`, not merged into main.
- `8e76f6f920f69a6eda1be8f4a42187db7b99dfc6`: fixed literal escaped newline sequences in `engine/world/TerraWeaveCavernsSeamGate.h` from Stage 16 (compiler blocker).
- `677e6a60e59a74818e2a89466300d27c285167c1`: `engine/world/TerraWeaveCavernsAsyncRebuild.h`
- `5cb533988640c0c401d8dc94f5a103b20b0d98fd`: re-audit collected results before new work scheduling.
- `0c6f9d81c12449757ac4af948d2205cc8deff01e`: `tests/terraweave_caverns_async_rebuild_test.cpp`
- `28c7ebe8586031ddbc1011e35804825a1aada3e6`: stage 17 addition to `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff is an additional commit; check branch HEAD for the final SHA.

## Implemented
- `CaveAsyncRegenerator`: bounded asynchronous C++20 CPU-only regeneration of requested seam chunks, with immutable `Settings` captured per task and a `CaveRebuildRequest` token. Worker calls `generateCaveMesh` only.
- `pump` collects finished work, re-audits any accepted pairs, and starts only available jobs, capped at `maxWorkers`. Duplicate in-flight jobs for one chunk are not launched.
- `poll` checks futures without blocking the host owner thread, accepts only current revisions through Stage 16 `CaveSeamPublicationGate::accept`, re-audits on acceptance and exposes accepted/stale/failed statistics. `drain` joins workers on teardown.
- Stage 17 unit source covers an incompatible coarse/fine synthetic seam triggering one CPU job, duplicate scheduling suppression and accepted async result.
- Stage 16 escaped-newline compiler blocker repaired before the next stage.

## Exact ownership / limitations
- CPU worker scheduler is not wired into existing AIVERSE shared executor; this uses `std::async` as an opt-in intermediate adapter. No direct `GeometryScene` or STRATA GPU submission, no `gfx::Device`, ECS, Box3D, SpatialWorld or NavMesh mutation occurs on workers.
- After acceptance the **owner** must regenerate world-state-compatible inputs, certify all adjacent seams and stage a whole matching shard/neighbor set, then publish only at safe render/physics barrier. No complete cross-system atomicity implemented.
- Workers rebuild `generateCaveMesh` using the base field, not `generateCaveMeshWithEntrances`; to support entrance shafts, authored carving and stamps, expose a versioned immutable density sampler in a later stage. Do NOT use this dispatcher as-is for modified cave volumes.
- Caller must bind an immutable settings snapshot to the request's sourceRevision and revalidate on world changes; dispatcher cannot infer whether a Settings object belongs to that revision.
- Failed jobs remain blocked; the caller must record failure and decide retry or explicit invalidation. No unbounded silent retry.
- `drain` blocks until workers complete; do not call it each game frame.

## Validation performed
- GitHub connector confirmed source, test, skill and handoff writes.
- Local container DNS resolution for github.com failed, preventing a full repository checkout. Existing local `/mnt/data/caverns_stage2` is only a partial earlier harness, not a source checkout for Stage 17.
- **Stage 17 not compiled or executed; no CMake/CTest, CPU regression, STRATA GPU, Box3D/NavMesh, Metal/GL/Vulkan acceptance run.** Test code is authored, not PASSED.
- Suggested full-checkout command:
  `c++ -std=c++20 -pthread -O2 -I. tests/terraweave_caverns_async_rebuild_test.cpp -o /tmp/caverns-stage17 && /tmp/caverns-stage17`
  then run all existing Caverns tests and real linked integration.

## Next steps
1. Convert the dispatcher to an AIVERSE job-system adapter using the same bounded immutable-job and stale-token rules.
2. Implement a versioned immutable field sampler for caves with entrances and author edits; regenerate certified, coherent neighbor pairs.
3. Connect the seam gate and batch shards to real GPU-fenced publish/retirement, with rollback-safe Box3D/SpatialWorld/NavMesh barriers. Verify no duplicate draws under STRATA fallback.
4. Register Stage 1–17 CMake tests and obtain actual CPU+GPU evidence. Fix compiler/integration issues before any main merge.
5. Add editor brushes, saved authored cave edits, and decoration/water integration after the structural runtime pipeline is safe.
