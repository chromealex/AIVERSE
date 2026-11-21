# AIVERSE MotionWeave / Universal View — Stage 145 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (OPEN / DRAFT)
Previous: [Stage 144](animation-stage144-named-playback-source-reload-2026-11-19.md)

## Objective and workflow

Continue implementation-first, then perform the user's requested **single comprehensive native test pass after all features**. Stage 144 added named graph phase continuity to generic `AnimationGraphInstance` and RTS/editor hosts, but the actual genre-independent presentation ECS source bridge rejected clock carryover whenever the Model publication epoch changed. Stage 145 makes that source/epoch decision safe and explicit without adding a second graph writer, gameplay authority, network/save format or GPU ownership.

**Status:** source implementation and GitHub blob-SHA readback completed; native compiler/CMake/CTest, Metal/GL/Vulkan, ASan/TSan, stress tests, benchmarks and currently passing CI **NOT RUN**. No feature-freeze or release-readiness claim.

## Branch baseline and commits

Started at Stage 144 head `63126fceb00e53f30df9183a6b7b32f6a85d0a39`.

Exact code commits:
1. `b8f5b80f3e7414e645f55d0c0d5b911d25e8e10d` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`: immutable rig signature capture and safe ECS playback handoff across publication epochs, new observability counters.
2. `61dd8c9cd4f97453b1edc5181da9711ddf0fbb4f` — `libraries/include/aiverselib/animation/EcsAnimationSystems.h`: new ECS GraphInstance reset treats an optional portable playback restore exception as a clean default restart; always consumes the one-shot pending snapshot after attempted restore.

Last code-head readback: `61dd8c9cd4f97453b1edc5181da9711ddf0fbb4f`.

GitHub branch ancestry snapshot: feature **ahead 1128 / behind 467** relative to `main`, merge base `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, DIVERGED. Relative to PR #22 base `feature/universal-entity-inspector-fps`, feature **ahead 1498 / behind 615**, merge base `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, DIVERGED. **No main/peer branch edit, merge, rebase, force push, PR retarget or release change.** Recheck ref heads before eventual coordinated disposable integration worktree.

## Design: identity, rig and generation

`MotionWeaveSourceBridge::sync(World&,span<AnimationSourceFrame>,bool)` still runs on the presentation owner thread before any MotionWeave update/scheduler worker dispatch. The source snapshot is validated before applying ECS structural changes:
- Each incoming source must have a nonnull Model and graph compiled against THAT Model; source IDs must be unique.
- The old generation of the **logical entity** must equal the incoming generation; a respawn, changed logical identity or a missing ECS instance ALWAYS starts from authored defaults.
- Exact skeleton hierarchy compatibility is evaluated using `RigHierarchy = vector<pair<string,int>>` of the **node names and parent indices**. Previous hierarchy is a copied immutable value per source, not a `Model*`; new hierarchy is copied during the current preflight. The old `Model` is never dereferenced to compare skeletons. At most one new hierarchy allocation is created per distinct (Model pointer, publication generation) within the same sync call; all actors using that asset share the immutable signature via `shared_ptr`, limiting repeated crowd overhead.
- On a Model publication epoch change, portable playback is eligible **only** when both old and new epochs are nonzero, the old and new Model objects have DISTINCT addresses, and the copied rig hierarchies compare equal. Changing the epoch with the SAME Model pointer requires recompiling the graph (the pre-existing strict guard) and a clean playback restart, because the old graph's clip metadata might have been mutated in place. Epoch change with the old Graph pointer unchanged remains invalid.
- A graph-only change at the same epoch can still carry phases when the source generation matches and exact rig identity is compatible, as previously intended.
- Playback capture requires an existing healthy, non-restarted ECS `AnimationGraphInstance` with the previous active Graph. It uses the existing **name-based** `snapshotPlayback()` and the ECS `pendingPlayback` one-shot handoff; no old rig, Model pointer, native operations, IK/contact, recovered physical pose, root motion, stale event queue or graphics handles are transferred.
- In the ECS animation update, `AnimationPlayerComponent` creates a new instance from the new graph. A compatible `pendingPlayback` snapshot restores state and clip phases. An incompatible state/clip simply leaves a clean default; any exception in OPTIONAL restore is contained, then the one-shot snapshot is dropped. Montage and graph-notify queues are cleared during asset change as before; neither becomes a second animation clock.
- `AnimationBridgeStats` appended `playbackCaptured` and `playbackDeclined` diagnostics. `playbackCaptured` counts accepted portable snapshots, not proven successful restoration by the downstream graph worker (which still validates named state/clip). The pre-existing `created/updated/restarted/removed/assetEpochRestarts` meanings remain.
- `sourceRigs_` copies are inserted on new/changed sources and cleared with `removeAbsent`, `forget` and `clear`; no strong Model/Graph or GPU owner reference is held in this metadata.

## Critical read-lease limitation

The old `RigHierarchy` comparison no longer reads the retired old Model, but `AnimationGraphInstance::snapshotPlayback()` still reads metadata of the **old compiled graph and its Model** (clip durations and names). Native hosts must keep the OLD CPU Model/Graph readable with an appropriate ModelReadScope/lease **until the synchronous source bridge sync completes**. A previously destroyed old Model cannot be recovered from a copied rig signature.

The existing `MotionWeaveSourceBridge::forget()` intentionally destroys the old presentation ECS source before resource invalidation. It therefore does NOT perform a cross-forget playback transfer. Stage 146 should introduce an explicitly bounded pre-retirement snapshot API for callers that need to detach old models BEFORE the next sync, then rebind by exact source generation and rig/epoch. Never blindly resurrect a gameplay entity or copy native solver pointers.

## Native compatibility and performance limits

- Core API remains genre-neutral. No RTS/FPS/editor classes imported into `aiverselib`. `AnimationSourceFrame` aggregate field order unchanged; `AnimationBridgeStats` diagnostics appended only. No ECS gameplay structural writes or actual animation evaluation occur inside `sync`.
- Copied hierarchy signatures are exact (no collision-prone fingerprint/hash), shared for actors bound to a matching asset during one changed-source sync. Signature capture allocates CPU memory and copies bone names only when a source is newly added or changed, not on every unchanged animation frame. Do not assume its cost is measured; benchmark high-crowd reimports in final pass.
- A host that in-place mutates a Model without incrementing its publication generation violates the established ModelPublicationEpoch contract and cannot expect safe continuity.
- Initial source registration and changed source updates may allocate ECS/map storage; this stage does not provide a transaction-wide rollback on OOM in existing `sync`. Portable playback snapshot allocation errors are individually caught and decline continuity rather than abort the new Model binding.
- The generic bridge does not own CPU old Model lifetime, GPU retirement tokens or graphics Device epochs; these remain host-specific ownership.
- No native tests were created/run because the user's requested order reserves test work for the final feature-freeze pass. Regression cases still needed: (same generation, compatible different-Model+new epoch) phase preserved; incompatible bone rename or parent changes restart; same-address reimport restarts; respawn restarts; graph-only replacement continuity; missing/removed sources dispose hierarchy metadata; no stale notifies, no double writer; optional capture/restore faults; crowd memory/performance; Metal/FPS/RTS/editor runtime parity.

## Actual verification and next Stage 146

**ACTUAL:** connected GitHub reads of `MotionWeaveSourceBridge.h`, `EcsAnimationSystems.h`, `AnimationGraph.h/.cpp`, Stage 144 handoff; exact SHA-guarded commits; source readback showing new preflight, epoch/rule comparison, metadata cleanup and exception-safe one-shot ECS handoff; branch ancestry and recent GitHub Actions status lookup (queued/cancelled older SHAs, not Stage 145 pass).

**NOT RUN:** native CMake configure, compiler/link, CTest, current CI pass, real GPU, sanitizers, visual parity, memory/profiling or final runner.

Next feature stage:
1. Add an explicit, bounded source pre-retirement clock-snapshot handshake (capture while old CPU Model is **still alive**, then detach old Model/Graph) for host reimport flows that invoke `forget()` before the next `sync()`. Keep logical entity generation and rig identities, reject stale epochs and reused pointer+epoch without proof.
2. Continue Universal View adoption and FPS/RTS/editor source/LOD/retarget continuity without duplication of the presentation ECS writer.
3. Review independent main/PR base divergence and contemporary renderer/model peers on a disposable integration worktree, not shared branch rebase.
4. Append separate Stage 146 handoff and AGENTS/journal/docs. Preserve the user-requested feature-first/full-tests-last workflow and Draft PR #22.
