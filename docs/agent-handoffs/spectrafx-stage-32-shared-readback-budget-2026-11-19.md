# SpectraFX Stage 32 — shared Metal checkpoint readback budget + pooled telemetry

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
HEAD observed before handoff commit: `8dccf95a52c60d6d6020c6ce0b7dc2dcc72f5a3e`
No merge/rebase/force push; mutations were made on the existing branch using GitHub's file-SHA guards.

## Changed sources and exact commits

1. `1926c45673613f288cce5b324cdfba2cad0e55a5` — **new** `engine/fx/SpectraCheckpointReadbackBudget.h`: a single-device, owner-thread C++ bandwidth/chunk admission limiter with in-flight reservation accounting and returned-byte telemetry; no new CMake source needed (header-only).
2. `80350f4e277ccbcd4e780e0a152765e70690a31e` — `engine/fx/SpectraGpuParticles.h`: optional shared limiter on `ComputeEmitterDesc`, plus `checkpointBudgetDenied` and `checkpointBytesReturned` telemetry.
3. `e25f876e2e416ee5f051a08fce66d767efdd3ffa` — `engine/fx/SpectraGpuParticles.cpp`: reserve readback bytes/chunks before enqueueing chunked async Metal checkpoint reads; `Pending` holds a shared reservation and returns capacity after callbacks/closure teardown, including failed partial enqueue; count actually returned, length-validated chunks. Budget denial is nonblocking and does not publish a partial snapshot.
4. `6840d2eeb606ace7f64e584621ed0be0cfcfe131` — `engine/fx/SpectraGraphRuntime.h`: opt-in `GraphRuntimeOptions::checkpointBudget`.
5. `13ce108d983aaad408267bc5854955b76192278a` — `engine/fx/SpectraGraphRuntime.cpp`: propagate shared limiter through strict GPU capability routing.
6. `3ebe5de6895f3ad9224c8b966b94da92970ab35a` — `engine/fx/SpectraGpuPoolWorld.h`: configurable default limits, optional externally shared budget, on-demand `PoolCheckpointStats` API.
7. `6122fc1f1740c035793925dfaeece92f96a7b916` — `engine/fx/SpectraGpuPoolWorld.cpp`: create one pool budget after CPU world init, inject it into every GPU group, include checkpoint cadence in group identity, compute on-demand aggregate checkpoint stats, reset budget on shutdown.
8. `e22c31354a5b32c1515363e6f087dd21e8074e22` — `engine/fx/SpectraGpuPoolWorld.h`: round-robin group-start state.
9. `595558ee26711163c4d51a5afa42c7cdcda7aae2` — `engine/fx/SpectraGpuPoolWorld.cpp`: rotate GPU simulation submission start for fairer checkpoint budget admission; draw order remains unchanged.
10. `0e392eba65ec2836af406c67dfd2084e1af4ecae` — `engine/fx/SpectraGpuParticles.cpp`: restore explicit Stage 31 stale/unsent-birth fallback reason lost in an intervening edit; no stale automatic imports.
11. `8dccf95a52c60d6d6020c6ce0b7dc2dcc72f5a3e` — `docs/spectrafx.md`: public Stage 32 C++ API, limits, and correctness boundaries.

## Architecture / API

- Only presentation-side `aiverse::spectra`, independent of FPS/RTS/any genre. Nothing enters deterministic gameplay ECS, authoritative rollback or Prisma postprocessing.
- `CheckpointReadbackBudget::tryReserve(frameSerial,bytes,chunks)` refuses if per-frame or in-flight caps would be breached. `settle(bytes,chunks,returnedBytes)` returns the reservation and accounts valid callback payload, not DMA measurements. Calls are **graphics-owner-thread only**, no concurrent-thread guarantee.
- Default caps: 512 KiB / 16 chunks each frame; 1536 KiB / 24 chunks in flight. Metal RHI itself can have up to 32 pending reads; the 8-read headroom is only a heuristic, not a guarantee when other subsystems enqueue work.
- A `GpuPoolWorld` has **one** budget for all its GPU groups. If two worlds, a standalone GraphRuntime, or other owners share the same Device, host must pass the SAME `std::shared_ptr<CheckpointReadbackBudget>` via `PoolWorldBudgets::sharedCheckpointBudget` and `GraphRuntimeOptions::checkpointBudget`. Otherwise only each world's own limits are enforced.
- Every ComputeParticles async request first allocates capture vectors, then acquires the reservation. A returned `false` on budget denial is not a failure of the GPU simulation step. No callback is invoked for rejected scheduling. The reservation survives partially accepted RHI reads and is settled by `Pending` lifetime/destruction after their callbacks are delivered or dropped.
- Pooled group keys now include `checkpointEveryFrames`, so two effects with different capture policies cannot accidentally share one group. Rotating step order reduces persistent starvation when groups share one budget.
- `GpuPoolWorld::checkpointStats()` is evaluated on demand and classifies active GPU groups by latest retained checkpoint as `freshGroups`, `staleGroups`, or `missingGroups`; `pendingGroups` may overlap with any of those. It sums request/failure/denial/received-byte counters per group and includes budget-global reservation diagnostics. A fresh/eligible checkpoint still does **not** prove lossless import or numerical parity.

## Verification: what actually happened

- GitHub connector: fetched current branch files before editing, all eleven source/doc writes succeeded. HEAD checked after changes as `8dccf95a52c60d6d6020c6ce0b7dc2dcc72f5a3e`.
- **NOT RUN**: local CMake/Clang build, Metal shader build, CTest, GPU hardware or editor, device-loss/partial enqueue callback fault injection, multi-pool scheduling stress, GPU/CPU parity, hardware readback bandwidth profiling.
- This is functional source implementation pending executable verification. It is not validated as production ready.

## Known limitations / risks

1. Admission is cooperative: independent GPU systems not sharing the pointer can still compete for the RHI's 32-read global limit. Even shared admission cannot guarantee actual RHI acceptance if unrelated clients consume slots.
2. If a device never delivers AND never destroys readback callback closures after a catastrophic loss, in-flight reservations may remain outstanding. Device teardown/lifetime behavior must be audited.
3. A callback payload is counted as returned bytes even if the whole multi-chunk checkpoint cannot be published. Byte counters are not exact hardware throughput.
4. Completed asynchronous snapshots may already be stale relative to new GPU submissions. Fallback refuses stale imports. Source identity, timed emitter RNG, CPU/GPU visual and VM parity remain out of scope.
5. Host may share one limiter across one Device. Reusing it after resetting/destroying the Device (frameSerial can reset) is not supported without a fresh limiter.
6. The round-robin ordering changes relative GPU dispatch ordering among independent presentational groups; validate on Metal, especially if render scheduling later introduces cross-group dependencies.

## Next Stage 33 (features before tests, per user preference)

1. Add adaptive checkpoint cadence based on recent budget denials, in-flight pressure and freshness, with bounded min/max and recovery when pressure subsides; never dispatch blocking readbacks in render.
2. Expose `checkpointStats` to editor VFX inspector and profiler; alert for continuously stale/denied groups without suggesting snapshots are authoritative.
3. Add generation-safe per-source GPU emitter lifecycle and shared group ownership if implementing persistent attached VFX; current pool supports one-shot only.
4. Once functional work is complete, compile C++/MSL and add smoke + injected-failure tests for budgets (frame reset, reject, settle, shutdown), callback cancellation, partial enqueue, fairness, multi-pool sharing, CPU importer atomicity and accurate fallback loss accounting.
5. Continue per-stage handoffs with actual branches, exact commit SHAs, files, tests RUN vs NOT RUN, limitations and next action. Avoid clobbering concurrent edits.
