# AIVERSE LENS — Stage 47A: GroundFX joins Roads/Shores/Fog OwnerPrepareCommit

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration, NOT main
Parent HEAD: 9f019ca7e52ffa1a2624c989772e349f948f86e9
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage46-fog-sheet-transaction-2026-11-20.md

## Implemented in source

- modules/rts/view/RtsGroundFx.h/.cpp: public opaque `PreparedFrame` via shared_ptr, `prepareFrame`, `commitFrame` noexcept, `drawPrepared`; `draw` retains compatibility as prepare+commit+draw.
- GroundFX prepare copies mutable CPU visibility memory, chunk metadata, look parameters, lane/territory cache, and borrows existing GPU Mesh handles. It prepares up to 16 chunks / 128 MiB meshes using NativeMeshBatch **into an isolated temporary clone**, never mutating published mesh targets.
- Explicit changed-mesh journal differentiates untouched borrowed buffers from replacements, including empty mesh deletions. The never-submitted replacement buffers are destroyed on abandoned PreparedFrame. After cohort prepare succeeds, commit retires exactly changed old meshes and publishes CPU maps and new GPU handles by no-allocation swaps. A full map/viewer reset retires the old map mesh owners instead of leaking them.
- Uses a publication revision check and one-shot committed guard. Invalidate/re-init/shutdown advance revision; stale commit is fail-closed.
- modules/rts/view/RtsView.cpp: adds GroundFX participant first in the existing shared owner transaction, then Roads, Shores, Fog Sheet and Fog Field; the Effects layer now emits only previously committed GroundFX GPU meshes.
- tests/tests.cpp: added late-owner rollback/retry and map-resize private candidate discard/retry fixtures, using existing fake RHI.

## Actual verification

- Updated branch source with GitHub connector and fetched files again after publication.
- Performed mechanical exact-target code edits and static checks. Did **NOT** run CMake, native C++ compiler, CTest, Metal/Vulkan/GLES or GPU parity tests. Assertions are unverified hypotheses pending the requested consolidated test run.

## Limitations / follow-on

1. This is **Stage 47A**, not a claim that all Terrain/Detail and GroundFX dynamic textures are in the same cohort. TerrainRenderer::build is called by separate RtsView::buildTerrain before frame draw; TerrainRenderer::draw still performs dynamic visibility texture, height-field and lazy LOD GPU updates. Stage 47B must give Terrain a proper externally owned candidate and commit hook (including detail incremental changes).
2. GroundFX drawPrepared still resolves/generated textures through TextureCache (cobble/dirt). These additions are not part of the owner transaction, and may fail after commit. Move texture generation to kit/init or staged preflight.
3. GroundFX preparing currently clones O(map cells + chunks + smooth territory blend) CPU state each primary frame. Profile and introduce a no-change fast path/copy-on-write sparse chunk journal before final release.
4. Frame-local GPU completion and resource owner lifetime require the existing host begin/retire hooks; no real backend tests were performed.
5. Remaining unified publication work: Terrain/Detail, sprites, MotionWeave, lighting, overlay view, RT secondary cameras, device loss and reload races.

## Next

Stage 47B: add TerrainRenderer::prepareMap/commitMap with external prepared CPU/GPU resources and preserve its existing incremental/detail path. Connect to same OwnerPrepareCommit where terrain source is live, and avoid legacy draw GPU side effects; then wire GroundFX read height policy to staged published terrain snapshot. Write tests for failing the last participant (Fog) after Terrain+GroundFX complete private preparation. Continue per-stage handoff, defer full CMake/CTest until requested full feature completion.
