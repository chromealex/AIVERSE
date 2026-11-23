# TerraWeave Caverns — Stage 36 streaming & world-residency handoff (2026-11-24)

## Scope / provenance

Repo: `chromealex/AIVERSE`.
Branch: `feature/terraweave-caverns-core-20261123`; **not merged into main**.

Stage36 commits:
- `ad095f9e81a26be53331fe481ae4820d59bf735a` — created `engine/world/TerraWeaveCavernsWorldResidency.h`
- `70822e271776172389d91476eb21ccf971e31415` — `CaveEditRuntimeBridge::dirtyKeys()` for streaming pins
- `23f69c85ef15b497120b94a580220aa10ca8adbf` — `CaveGameplayEditHost::pendingEditKeys()`
- `31c997b9812c173d3eafa56f41dc7af8352c651e` — `CaveGameplayRuntimeFrame` residency/edit pin integration
- `ea514318d89eb09d555a772a05a8372c258f9ca3` — capacity/retirement backpressure
- `5634b8b62a9d8a4987c469ffba31b21c3e196f31` — created `engine/world/TerraWeaveCavernsStreamJobs.h`
- `70dd9afaa657410476cd34885cdc72270526e37b` — ticket-current predicate for async completion validation
- `7ca9d0fdccdb4d5fd71d25f23be00f41d7024848` — versioned immutable snapshot getter for stream jobs
- `21f74ebd7f208c7c144d3909632e48fdd951b591` — CPU dispatch, pending retires and stale result discard in runtime frame
- `b765eb8def3ef867d4f24b68452b0b5d9893f316` — failed worker results carry their original ticket
- `59e5e36f48ef0b65215ea003ae0919df5cd6ee93` — failed work retry and configured stream worker budget
- `0fa0fcbf22c667d12d19e321ee16b5d38fc8fe42` — signed world-to-stream chunk helper and bounded positions
- `ec0e1ff59a1236b46b25fd00bba5abfcd24d656b` — owner-commit gated native Build/Retire ACK, retained CPU result
- `bb77877a3ea18db599a5831a18481f90649c748f` — pinned edit priority and closest-camera scheduling
- `8526f8b28fadd9506a44639530eca6901c6396b4` — explicit per-world open/close lifecycle, configurable residency capacity
- `0c74831909175016ff7cf6966b2f7f890a6183ab` — committed GPU shard catalog forget-on-retirement hook
- `3eb6dd5431e69386d59f3c1159a410c1279d5226` — loaded-neighbor seam certification invalidation upon unloading
- `31f765cd638248a9aa62613ebaa6ebd77de5c116` — world host bookkeeping retirement hook
- `371bab86ce7ef71d14289534b7a7495eb1f0a307` — native-publication-gated metadata registration/removal
- `26ee011d792bf92b514aabb839203e42646a43fb` — Caverns owner routing in `AGENTS.md`
- `df6a340044ca87474538e5c9cbb1e4eee8c9ac98` — Caverns skill update
- This handoff commit follows these. Read branch HEAD for current tip.

## Implemented features

### Versioned resource intent and camera-centered streaming

`CaveWorldResidency(StreamSettings)` owns external resource residency metadata (not gameplay ECS state): desired chunks centered on signed `ChunkKey3`, configurable horizontal and vertical radii, max concurrently occupied slots, max build and retire ticket count per pump. `caveStreamingChunkAt(worldCm,resolution,spacingCm)` converts signed centimetre world positions with proper floor division for negative coordinates.

`pump()` prioritizes pinned resources, then closest camera chunks, and returns `CaveResidencyTicket{Build|Retire,key,generation,sourceRevision,lod}`. Resident capacity is also occupied by pending native retirements: it is not reused until the exact retirement receives a successful host ACK. Stale build/retire results cannot act on a newer generation. A failed ticket increments generation for retry. When a camera returns to a retiring chunk, a generation change invalidates the old retirement intent before it can remove the resource.

### Leases for in-progress edits and other owners

`pin(key,CaveLeasePurpose::Edit|Worker|Reader|Publication)` returns a unique lease token, `unpin(key,leaseId)` releases it. A pinned item is not retired by camera movement. Both stored SDF edits and CPU worker jobs remain versioned; generated GPU meshes are not accepted merely because a worker finished.

The existing Stage22 `CaveEditRuntimeBridge::dirtyKeys()` and Stage34 `CaveGameplayEditHost::pendingEditKeys()` expose all uncommitted edit chunk keys. `CaveGameplayRuntimeFrame::syncStreamingEditPins()` automatically pins those keys and removes pins once gameplay dirty ACK has cleared them; it uses the monotonic Stage34 runtime revision, so Undo/Redo and later source edits invalidate old streaming tickets.

### Actual bounded asynchronous CPU build jobs

`CaveWorldStreamJobs(workers)` asynchronously builds `CaveMesh` against a frozen `CaveVolumeSnapshot`. It limits concurrent jobs, allows only one outstanding job per key and returns the original ticket with the generated surface. Worker failure is returned with `success=false` and the original ticket, allowing the owner to fail/retry the exact version rather than getting stuck building forever. Workers do not access gfx::Device, renderer, physics, navigation or ECS world state. Full drain is explicit on world teardown.

### Runtime lifecycle APIs

`CaveGameplayRuntimeFrame` now holds the residency manager and stream job queue (worker count configurable). It adds:
- `openWorld(initialLoadedBounds,snapshot)` and `closeWorldAfterGpuIdle(device)`, single-world instance lifecycle
- `setStreamCenter`, `setStreamLod`, `pumpStreaming`, `streamingStats`
- `dispatchStreamingCpu(cameraChunk,resolution,spacing)`, `pollStreamingCpu`, `outstandingStreamRetires`
- `pinStreamChunk`, `unpinStreamChunk`, `acceptStreaming`, `failStreaming`
- `publishStreamBuild(ticket,publishedCertifiedChunk,generatorVersion,exclusiveProof,actualNativeCommit)`
- `publishStreamRetire(ticket,exclusiveProof,actualNativeCommit)`

Build results are retained in `readyStreamBuilds_` until a genuine external native commit. The successful Build callback must have published matching GeometryScene + Box3D + SpatialWorld + NavMesh at an exclusive owner barrier; then the exact `CaveCertifiedChunk` is registered in the committed shard source catalog and seam gate before the ticket is ACKed. A confirmed Retire callback must actually have unloaded those native resources, after which source catalog, seam certifications and owned physics bookkeeping are discarded and the native retirement ticket is ACKed. After any post-commit metadata failure, the Stage35 world barrier requests recovery rather than silently running divergent versions.

A new SDF edit clears queued CPU build results and pending native retire intents (their generations have been invalidated), then re-syncs required edit leases. After successful world shutdown, further updates are refused; construct a fresh owner for another world.

## Boundary and limitations — do NOT overclaim

1. These are **real streaming and lifecycle API features**, but **the runtime is not yet automatically instantiated or driven from a specific `apps/client`, dedicated-server or `GameCore` owner loop**. The game client must feed camera chunk positions, actual GPU/physics ownership and callbacks. Never call gfx APIs inside deterministic `GameCore` ECS ticks.
2. The Build and Retire callbacks are **not built-in native resource mutations**. The external game owner must truly stage/publish/remove a complete affected shard plus matching Box3D, SpatialWorld and NavMesh state. The Stage36 ACK handshake cannot prove whether a callback lied; a dummy `return true` is forbidden. Full native owner attach and recovery are still outstanding.
3. Stage35 final Box3D old-body destruction can fail irreversibly. The caller must keep readers suspended and rebuild from authoritative snapshot upon `RecoveryRequired`; neither Stage35 nor Stage36 yet has an automatic recovery rebuild.
4. Streaming source-revision changes conservatively invalidate loaded readiness for all tracked chunks. A spatially incremental version dependency map is a future optimization. CPU streaming jobs and edit jobs use separate bounded worker pools and may temporarily have overlapping work for the same key; a shared work scheduler is desirable.
5. Native GeometryScene/STRATA index retirement still may require a real full GPU-idle proof. Stage36 tracks resource leases and ACKs but does not implement asynchronous fence retirement itself.
6. Bootstrap using `registerCommittedMesh` is metadata-only. Actual visual, physics and nav source registration must be coupled with external native publication. Never draw the same cave twice.
7. A large uncommitted edit can exhaust `StreamSettings::maxResident` while acquiring Edit leases; this fails closed and requires a larger configured stream capacity or staged smaller edits. The world owner must not ignore that error and continue retiring resources.
8. Changing cameras or world versions makes earlier tickets stale; callbacks must check `CaveWorldResidency::current` before mutation and use a held exclusive owner barrier until ACK.
9. The closure API drains CPU jobs and GPU owners, but world-specific Box3D/SpatialWorld/NavMesh unload remains the external lifecycle owner's responsibility.

## Tests actually conducted

The user explicitly requested **finish features first and defer tests**. Stage36 therefore did NOT run native C++ compiler/linking, CMake/CTest, GPU/STRATA screenshots, Box3D, SpatialWorld, or nav acceptance, and no new Stage36 test executable was added. GitHub connector confirmed repository writes and commit SHAs only; they are not compilation proof. Earlier Stage33's four local CPU smoke results do not validate Stage36.

## Next Stage37

1. Complete mixed-LOD neighboring cave geometry transitions in all ±X/±Y/±Z faces, including changing camera/stream distance and authored Carve/Fill boundaries without cracks.
2. Integrate stream build/retire callbacks with the actual client/server frame owner and stage/commit complete GeometryScene + Box3D + SpatialWorld + NavMesh sets. Keep all reader/lease barriers and strict revision ACKs.
3. Build a recovery path from `RecoveryRequired` that retains the last good SDF revision and reconstructs the native world without stale GPU owners or lost collision/nav state.
4. Keep the user-requested feature-first order: no broad test campaign until feature completion. Update `skills/world/aiverse-terraweave-caverns/SKILL.md` and create dated `docs/agent-handoffs/caverns-stage37-2026-11-24.md` after next implementation.
