# TerraWeave Caverns — Stage 32 handoff (2026-11-23)

## Repository and commits

Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`. **Not merged into main.**

Stage32 commits:
- `841c124a2a5b1903a3e28a172dcdbf8701f79f44`: `engine/render/TerraWeaveCavernsPreviewPacer.h`, bounded per-frame upload and retry policy.
- `85dac54a3cccec6695beba2efcaa4d31c694ce90`: `engine/render/TerraWeaveCavernsShardGpuHost.h`, GPU scene/resident/retirement counters.
- `764355e227de24cd1ef496f29ce8b17e25a6475f`: `apps/editor/Editor.h`, GPU preview state and diagnostics.
- `a8f6654e68696684f6b974c8563d8f6fe12ece78`: `apps/editor/Editor.cpp`, incremental shard upload pacing and delayed activation.
- `d3724033c28d62c955df2a524c86716de2c3187d`: `apps/editor/windows/TerrainWindow.cpp`, display GPU source/retirement/retry stats.
- `29e8b8b8977e625884046002930e9976cc5f6f23`: `tests/terraweave_caverns_preview_pacer_test.cpp`, regression source.
- `14d15e0ca0a34f12fae2f81f56e024642a19fb8e`: `skills/world/aiverse-terraweave-caverns/SKILL.md` update.
- This handoff is a subsequent commit; inspect branch HEAD for current SHA.

## Design and changed API

`CavePreviewPacer` is a pure C++20 frame policy. The GPU owner may stage at most one shard per device frame, and failure triggers a 30-frame cooldown (configurable in its constructor). A new `authorEpoch` resets all pending allocations to a new series; completed stages and staged candidates are counted and observed independently.

`CaveShardGpuHost::stats()` reads live resident bookkeeping from its `CaveGeometrySwapOwner` instances, reporting active shard scenes, pending shard revisions, number of retained GPU scene owners, and active render revision. Counters are diagnostic; they are not GPU memory measurements or guaranteed GPU-fence completion evidence.

`Editor::CavernsEditHost` no longer calls all-shard GPU staging in one Scene frame. It obtains current certified `CaveEditRuntimeBridge::visualPreviewCandidates`, converts them to `CaveBatchChunk`, groups by `groupCaveEditsByShard`, and retains staged group plans across frames. One `CaveShardGpuHost::stage` call is admitted per device frame. All staged group scenes remain unpublished until all shards stage; only then is `activateVisualOnly` called. On a newer MapDoc revision, partially staged sources are canceled; on temporary GPU staging failures, the current old visual scene remains and the attempted upload is retried after cooldown.

The TerrainWindow Caverns panel now shows GPU preview: active groups, staged groups, remaining uploads, GPU scenes retained for retirement, visual revision, failure count, and retry-after frame. It continues to say Box3D/SpatialWorld/NavMesh are unchanged. `Editor::cavernsBuildStats.visualCommitConnected` is intentionally false: that flag represents real gameplay publication, not this visual-only path.

## Actual validation

- GitHub connector acknowledged each source, test and skill update, with returned commit SHA.
- **PASS:** isolated C++20 reproduction of the new `CavePreviewPacer` policy compiled with `-std=c++20 -Wall -Wextra -Werror`, then executed. Local command:
  `c++ -std=c++20 -Wall -Wextra -Werror -I/mnt/data/caverns32 /mnt/data/caverns32/tests/test.cpp -o /mnt/data/caverns32/check && /mnt/data/caverns32/check`.
  Actual output: `stage32 pacer PASS`. Checks one upload/frame, 5-frame retry delay for the test configuration, ready state, stale epoch and reset. The GitHub regression also adds a zero-retry rejection check; that precise uploaded source has not been executed separately.
- **NOT RUN:** full AIVERSE editor C++ compile/link, native CMake/CTest, hardware GeometryScene uploads, STRATA GPU rendering, a Scene screenshot, Box3D/SpatialWorld/NavMesh acceptance, device-loss/fence failure injection. Isolated policy success is not an end-to-end engine pass.

## Limitations and next steps

1. This pacing is per shard **call count**, not a GPU millisecond or byte budget. Large individual `GeometryScene::loadPrepared` uploads can still block. True nonblocking GPU streaming and separate STRATA sidecar retirement remain incomplete.
2. `CaveShardGpuHost` holds previous full scenes until explicit `shutdownAfterGpuIdle`, and its GPU-owner cap can cause backpressure; retry cooldown is intentionally bounded and may keep older visuals until GPU reclamation. Add an explicit safe retirement collection policy only at confirmed GPU idle boundaries.
3. `CaveShardGpuHost::activateVisualOnly` continues to be an EDITOR-ONLY render commit. Neither Box3D nor SpatialWorld nor NavMesh is changed or ACKed. A future game owner needs rollbackable native subsystem visibility and an exclusive frame/tick barrier before calling `CaveEditRuntimeBridge::publishReady`.
4. On authoring changes occurring during an upload, the editor cancels outstanding staged GPU resources and restarts after the latest CPU snapshot is complete. Native interaction, project transitions, rollback from GPU failures and shutdown were NOT validated on device.
5. The existing `Editor::renderScene` draws cave preview geometry once when `cavernsVisualPreviewEnabled` is true and the flat world project matches. No new double-submit path is added in this stage.
6. **Highest priority:** get a full AIVERSE checkout, compile and run the native editor with CMake/CTest tests for all Caverns stages1–32, fix real build/runtime errors, then run Scene GPU captures with repeated Carve/Fill, mixed-shard Undo/Redo and device-loss cases.
7. Continue skill updates and separate dated handoffs after every stage. Do not merge feature branch into main until real native verification.
