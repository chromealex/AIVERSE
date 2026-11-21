# AIVERSE MotionWeave / Universal View — Stage 146 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT at start of stage
Previous: [Stage 145](animation-stage145-generic-ecs-source-epoch-playback-2026-11-19.md)

## Objective and user validation rule

Stage 145 preserves compatible playback across different Model publication epochs when old CPU Model/Graph remain readable until `MotionWeaveSourceBridge::sync()`. However, hot-reload hosts sometimes must call `forget()` BEFORE invalidating CPU/GPU assets, destroying the existing presentation graph player. Stage 146 implements an explicit, bounded pre-retirement snapshot-and-detach API, then hooks it into the generic View and two source/graph reimport paths of the legacy RTS host. It does NOT transfer authority, root motion, physical recovery, IK/native operations, pose matrices, notifies, montage stacks, GPU ownership, or legacy RTS animation clocks.

**The user explicitly requires finishing feature development first, followed by one comprehensive native test pass.** Accordingly local build, compiler, CTest, sanitizer, real GPU, perf benchmark and fresh green CI were NOT RUN. No claim of production parity/feature freeze. Do not merge this draft PR on the basis of source-level review.

## Exact implementation commits

1. `619f32a12012666a4baaf0ca421d85e784ea3df3` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`: initial bounded pre-retirement playback capture/rebind with protected 120-sync expiry and 8192-record cap.
2. `8c0a4a0824d8b378bb338671f767e8bc36f682a4` — same header: correct capture to use `World::tryGetConst<T>()` for read-only ECS queries and add required standard headers.
3. `2233ae00a85f5d10ec2a62f1873344902a32ce67` — `engine/view/View.h`: expose opt-in `View::forgetSourcesForReimport(ids)` while preserving original `forgetSources(ids)` semantics.
4. `c614ee3fe7d7abffc204883b9597bb6d1b5b9583` — `modules/rts/view/RtsLook.cpp`: use opt-in pre-retirement detach at two existing graph/model source reload sites; unrelated generic source removals continue to discard playback.

## Public API and exact lifecycle

```cpp
// Presentation owner thread, before old Model/Graph CPU lifetime ends:
view.forgetSourcesForReimport(sourceIds);
// Host may now queue its old models for GPU retirement. It must retain its
// own proper graphics lifetime tokens and worker synchronization.
// On next presentation frame (new Model + newly compiled matching graph):
view.update(objects, dt); // internally bridge.sync(...)
```

`MotionWeaveSourceBridge::forgetForReimport(World&,span<const uint64_t>)` captures optional `AnimationGraphInstance::PlaybackSnapshot` (named state, finite portable clip phases), then destroys old presentation ECS entities synchronously and clears their active Model/Graph references. Only a healthy, nonrestarting player bound to a valid old compiled graph, a nonzero old publication epoch and a saved copied rig is eligible for capture. OOM/graph errors in optional capture fail closed to ordinary forget. `forget()` remains strict, discarding both ECS and any pending snapshot. `clear()` also drops all pending snapshots.

Snapshot metadata is a **value-only** logical generation, old publication epoch, immutable copied name/parent rig hierarchy, named clock, retirement sync serial and an integer-only old address guard (no dereference or old Model/Graph pointer stored). A later `sync()` consumes each pending record **exactly once** even on mismatch and can initialize only the NEW presentation entity's `AnimationPlayerComponent::pendingPlayback`. Eligibility requires the SAME logical source ID and generation, exactly matching new rig hierarchy, explicit distinct NONZERO old/new epochs, and a DIFFERENT model address. Recycled identical address, old rig mismatch, zero/reused epoch, generation change or missing old player all restart at authored defaults. Actual named state/clip compatibility is still checked in `AnimationGraphInstance::restorePlayback` by the existing ECS worker; `playbackCaptured` counts an accepted one-shot seed, not proven restoration.

Unused records expire after 120 successful `sync()` calls (not wall-clock seconds or Device frames). At most 8192 records are retained and oldest entries are evicted when at capacity. Pruning runs during source `sync` and opt-in detach. No animation worker or renderer runs in this API. Existing identity/graph validation still occurs before source world mutations; it does not give `sync` transaction-wide allocation rollback.

In the existing RTS host, `reloadModelSources` and graph/appearance source invalidation now request portable generic ECS clock capture while their old asset graph is still accessible, before the previously established native/GPU Model retirement and legacy per-entity state replacement. The independent RTS legacy `ReloadPlayback` snapshot path from Stage 144 continues unchanged; it is NOT used to seed the generic ECS owner.

## Read-lease and compatibility limits

The host MUST call `forgetSourcesForReimport` while the old CPU model and graph remain alive and no scheduler jobs are running; the bridge will read the old compiled graph to capture clip metadata. This call does not acquire external ModelReadScope leases or GPU fences. It is illegal to invoke concurrently with the MotionWeave worker update.

Old and new publication epochs must be generated by the host and advance when the asset changes; zero and reused epochs are rejected for cross-forget recovery. Same-address or allocator-reused Model storage is rejected conservatively, not asserted safe. If an old Model was already destroyed before capture, phase recovery cannot be attempted.

The new API is fully genre-independent; the legacy RTS host is only an adapter. No changes to `AnimationSourceFrame` aggregate field order, serialization, network state, simulation checksums, device/texture ownership, graph JSON schema, montage event semantics, or editor authoring binary format.

## What was actually checked

**ACTUAL:** read Stage 145 handoff; read source from connected GitHub for `MotionWeaveSourceBridge.h`, `EcsAnimationSystems.h`, `AnimationGraph.h`, `engine/core/ECS.h`, `engine/view/View.h`, and the two affected `RtsLook.cpp` call sites. Reviewed the fact that `World::get` is not const and corrected the initial read-only capture implementation. GitHub returned the four exact code commit SHAs. New bridge source blob was read back after the first commit. No local Mac checkout was mounted into this environment.

**NOT RUN:** CMake configure, native C++ compile/link, CTest, current-head CI pass, Metal/GL/Vulkan renderer validation, FPS/RTS/editor visual parity, source-reimport stress, fault injection, sanitizer, benchmark, final `bash tools/animation/validate-final.sh`.

## Remaining gaps and Stage 147

1. Implement/cross-check transparent host support for **stable publication generations** in generic FPS/RTS/editor adapters; verify live RTS reimport actually advances epochs and does not accidentally fall back to zero. Preserve exactly one animation ECS writer.
2. Complete remaining Universal View static/animated/LOD/attachments and source import/retarget parity, while avoiding genre-specific dependencies inside `engine/view` and `aiverselib`.
3. Add final feature-freeze coverage for successful same-ID generation+rig cross-forget clock retention, changed source generation, incompatible rig/clip, reused epoch/address, expiration/8192 eviction, ordinary forget, clear, failure-injected capture, no stale notifies, no double graph writer, and high-crowd memory.
4. Coordinate with the many separately advancing `main`/renderer/texture/geometry branches using a **disposable integration worktree** later; do not silently rebase, force-push, auto-merge or retarget Draft PR #22.
5. Append a standalone Stage 147 handoff and active journal entry. Only after all genuinely required functionality is complete run the single requested comprehensive native Mac pipeline (`bash tools/animation/validate-final.sh` plus GPU/visual proof), fix failures, then assess PR readiness.
