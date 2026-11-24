# TerraWeave Caverns — Stage 30 handoff (2026-11-23)

## Provenance

Repo: `chromealex/AIVERSE`; feature branch: `feature/terraweave-caverns-core-20261123`; **no main merge**.

Actual Stage30 GitHub commits:
- `f9feb0b606c8503a5f639a1ece4edc2a1304ffa3`: `engine/render/TerraWeaveCavernsShardCatalog.h` capacity and full-source validation fix.
- `d2ed256cee7706cc0e04a546001f5d8947189d34`: new `engine/render/TerraWeaveCavernsShardGpuHost.h`, graphics-owner per-shard GeometryScene staging and explicit host commit.
- `65e872cf31ce8cae1393974d6af07a9f76209776`: deterministic `groupCaveEditsByShard` and duplicate-key prevention.
- `3cf58fbbda4425b265e2eba87da04266e1220b0f`: new `tests/terraweave_caverns_shard_gpu_host_test.cpp`.
- `4ea96c8785710e80ed25453437eb3a5ce3df31b7`: owner-exclusive `CaveShardGpuHost::activateGroup`.
- `10b7bf318b0a572e842872fd33bafb932404693c`: Stage22 CPU `CavePreparedEdit` to full per-shard GPU staging adapter.
- `631517a2aecbda6a40ce3488a3b96be1683cf549`: mandatory update to `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This dated handoff is the subsequent commit; check feature branch HEAD for tip.

## Architecture and implementation

`CaveShardGpuHost(CaveShardConfig)` owns a committed `CaveShardCatalog`, a `CaveGeometrySwapOwner` per loaded spatial `CaveShardKey` and pending complete-shard source snapshots. It stages actual `GeometryScene::loadPrepared` owners via the existing Stage24 GPU adapter, retains unrelated active shards, and does NOT change active draw while preparing edited geometry. A completely empty group uses `stageEmpty`. A mixed group includes all unchanged and updated source slots so a Fill deletion cannot erase neighboring cave sources.

`stage(device,shard,editedChunks,renderRevision,options)` computes a complete CPU preview, builds the GeometryScene models/manifest (or zero-geometry tombstone), stages the GPU owner, then records a pending versioned shard snapshot. Failure leaves the committed catalog and previously active draw unchanged. Partially failed GPU uploads must be recovered under a real device-idle lifetime barrier.

`stagePreparedEdits(device,vector<CavePreparedEdit>,generatorVersion,batchRenderRevision,outputShardKeys,options)` is the new direct bridge from Stage22 `CaveEditRuntimeBridge::publishReady` candidates. It requires consistent sourceRevision, translates each candidate to a batch entry, groups across signed spatial shards, stages each complete shard and cancels stages started in the current call if any stage fails. It does not auto-ack dirty queue and does not own game physics.

`activate(shard,externalCommit)` switches one staged shard only after host callback confirms global owner visibility. `activateGroup(shardKeys,externalBatchCommit)` validates ALL shard pending versions, makes one host callback for the entire change and switches all GPU shard owners with exclusive renderer/physics/nav access. It adopts the complete per-shard source catalog only after GPU activation. A successful callback must be a REAL all-owner atomic changeover, not a dummy acknowledgment. If that host callback partially changes physics and returns false or throws, this API CANNOT roll back it.

`draw(renderer)` uses one GeometryScene per active shard, not duplicate per-chunk paths. `collectAfterGpuIdle` and `shutdownAfterGpuIdle` are owner-thread cleanup requiring actual GPU idle; this code does NOT yet provide nonblocking per-source STRATA fence retirement.

`groupCaveEditsByShard` now provides deterministic grouping and duplicate rejection. `CaveShardCatalog::adoptSnapshot` validates its configured 1–4 chunks-per-axis capacity and checks source geometry/revisions without a hard-coded 64-entry assumption.

## Files changed

- `engine/render/TerraWeaveCavernsShardGpuHost.h` (new)
- `engine/render/TerraWeaveCavernsShardCatalog.h`
- `tests/terraweave_caverns_shard_gpu_host_test.cpp` (new)
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This handoff document.

## Verification performed and NOT performed

- GitHub tool confirmed creation/update of all source/test/skill commits and returned the commit IDs above.
- The existing Stage12/Stage24/Stage29 source structures were inspected via GitHub before modifying.
- **NOT RUN**: Stage30 C++ compilation, native full AIVERSE CMake/CTest, GPU uploads on Metal/Vulkan/GLES, STRATA runtime selection, graphics Scene integration, physics (Box3D/SpatialWorld), NavMesh gameplay or end-to-end edit/screenshot. `tests/terraweave_caverns_shard_gpu_host_test.cpp` is source-only, not PASS evidence.
- Suggested next native action: compile `terraweave_caverns_shard_gpu_host_test.cpp` in the full engine's real render/physics targets, then run all Caverns stages1–30 and native editor Scene acceptance with fill/remove/undo and cross-shard visibility.

## Limits and next stage

1. The new GPU host exposes a real **graphics staging API** but is NOT wired to `Editor::renderScene` or its lifecycle. Stage23 editor still only pumps CPU jobs and reports `visualCommitConnected=false`. There is no user-visible newly painted cave without explicit host integration.
2. Stage25 owner readiness is bookkeeping. Stage26 Box3D immediately creates active bodies during commit, and separate SpatialWorld/NavMesh changes still lack guaranteed rollback. A real external all-or-none owner barrier and recovery are required; do not return true from a no-op `activateGroup` callback.
3. GPU STRATA sidecar retirement still can require `gfx::Device::waitForResourceIdle()`. Whole-shard GeometryScene reloads can be expensive; no nonblocking GPU-fenced retirement was demonstrated.
4. There is a potential failure gap between a host-wide successful callback and later per-shard GPU activation/catalog bookkeeping; all owners must be exclusively quiesced for the entire operation. For production, move to a single infallible publication point/transaction with fully staged resources before acknowledgment.
5. This host performs separate per-shard GeometryScene reloads with bounded source counts. Large edits spanning many shards need a top-level bounded transaction limit, GPU allocation pressure management and frame pacing.
6. Next: wire native Editor Scene to a persistent `CaveShardGpuHost` as one authoritative drawing source, *but only after* a real Box3D/SpatialWorld/NavMesh owner commit is implemented or show an explicitly visual-only (non-gameplay) mode. Add live GPU resource lifetime cleanup, implement true owner rollback/inactive Box3D staging where possible, run native compilation and screenshot acceptance. No main merge before evidence.
7. Per-stage skill and dated handoff are mandatory. Continue with Stage31 handoff for all subsequent source changes.
