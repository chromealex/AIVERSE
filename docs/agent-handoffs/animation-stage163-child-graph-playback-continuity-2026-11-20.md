# MotionWeave Stage 163 — independent child playback continuity (2026-11-20)

Branch: `feature/universal-ecs-animation-pipeline`
Starting point: Stage 162 (handoff `docs/agent-handoffs/animation-stage162-modelview-independent-child-graphs-2026-11-20.md`)
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 (keep DRAFT/unmerged)

## Goal and code delivered

Carry child Graph state/clip clock positions through staged ModelView parent+child Model/Graph reimport without retaining old Model/Graph/native pose pointers. A parent actor's existing Animator retains its own independent phase handoff from Stage 157. Animation is fully genre-neutral.

1. `engine/view/AnimatedSceneAttachmentSources.h`, commit `6cdfd4626dbdba9475c6a009af3ce8d51cbda437`:
   - `PlaybackHandoff` holds ONLY a portable `AnimationGraphInstance::PlaybackSnapshot` and copied `(node name,parent index)` rig hierarchy. There are no old Graph/Model/ECS pointers.
   - `capturePlayback(Key)` reads the healthy, currently-owned Graph player for that child only. Declines missing/stale/non-healthy players, incompatible source publication or empty state.
   - `queuePlayback(Key,PlaybackHandoff,incoming Model)` accepts a one-time seed for a NEW child source ONLY when all incoming node names/parent indexes exactly match. Duplicate key or missing identity is rejected; incompatible models restart from authored defaults.
   - `sync` seeds `AnimationPlayerComponent::pendingPlayback` AFTER the bridge creates the new child ECS entity and BEFORE its ONE `stepAnimationPresentation`. Existing Graph worker calls `restorePlayback` on first initialization; that operation validates named state/clip matching and declines renamed/changed clips. Pending records are one-shot and cleared, as are `clear()` retirements. `Statistics` now exposes queued-seed acceptance/decline counts (not success of restorePlayback).
2. `apps/modelview/main.cpp`, commit `5061649e8aa873a7cacec35b365862f1ce33a1ca`:
   - After all candidate Models/Graph publications and per-actor scene children are staged, but BEFORE committing new assets or retiring old CPU Models, match old vs new per-actor `{scene=1, object=actor-N, childId}` identity.
   - Capture old Graph playback, compare to incoming published child Model rig, and queue safe named playback seed in the fresh generation's `AnimatedSceneAttachmentSources`. Rejected or absent snapshots do not block valid reimport. Log `queued` and `declined` counts.
   - The new source is still advanced exactly once at its normal owner pre-render boundary after atomic replacement. No second evaluator and no cross-World ECS handles/old poses survive.

## APIs

```cpp
using Sources=aiverse::view::AnimatedSceneAttachmentSources;
Sources oldSource, newSource;
// Old sources must already have been stepped with old model/graph owners live.
Sources::Key key{1,"actor-0","lantern"};
auto snapshot=oldSource.capturePlayback(key);
if(snapshot && newSource.queuePlayback(key,*snapshot,*newPublishedModel)) {
    // At next owner pre-render boundary: newSource.sync(scenes,cache,...)
    // restores compatible named state+clip clocks before Graph update.
}
```

Graph parameters are NOT a portable playback clock. The new immutable published Graph's `AppearanceAssetRegistry::Snapshot::parameters` remain the authoritative authored parameter overrides on every subsequent `sync` (applied by `AnimationInputsComponent`), avoiding accidental retention of obsolete runtime-procedural values. Live host override *state* beyond authored appearance is not implemented.

## Risks and not-yet-implemented work

- A one-shot seed is *queued*, not guaranteed to be restored: the new Graph may rename a state/clip, and `restorePlayback` then restarts at authored defaults. There is no restore success telemetry from the worker in this stage.
- Rig identity only compares node name and parent order; retargeted rigs with equivalent bone mapping but reordered nodes deliberately decline; there is NO cross-rig state/pose transfer.
- No restoration of controller transient state, subgraph internals, event streams, notifies, montage, root-motion, contacts, physics, native operations, morph state or worker caches.
- There is NO fully transactional exception atomicity through a throwing graph evaluation; errors remain reported by the existing sync API and source must re-sync on the next owner boundary.
- The ModelView stage 162 code is still NOT natively compiled; all these commits are SOURCE IMPLEMENTATION ONLY.

## Actual verification

ACTUAL: connected GitHub read of current `AnimatedSceneAttachmentSources`, native ModelView generation transaction, `AnimationGraphInstance::PlaybackSnapshot`, `MotionWeaveSourceBridge` and `EcsAnimationSystems`; SHA-guarded source commits and code readback.

NOT RUN: native Mac C++/link/CMake, CTest, CI, ModelView playback hot reimport, real clip phase pixels, Metal/Vulkan/GLES, ASan/TSan/UBSan, VRAM/large-crowd perf, fault injection. Do not call this verified or complete until acceptance.

## Readiness matrix

| Area | Source status | Outstanding |
|---|---|---|
| Genre-neutral ECS MotionWeave, imported Graphs | Incremental integrated | native regression suite |
| Independent child Graphs Studio / ModelView | Stage 161 / 162 source implemented | compiler, real 3D skinning and multi-actor tests |
| **Child Graph named playback across native ModelView reload** | **Stage 163 source implemented** | compile, names/rig mismatch and phase continuity tests |
| Authored animation parameter overrides | Existing per-frame `AppearanceSnapshot` input | live parameter override handoff if needed |
| Complex LOD/morph/retarget | Partial | rig visual/perf parity |
| Scene runtime bridge | Not implemented | peer multiworld scene branch integration |
| GPU resource lifetime, full provider token retirement | Not implemented | shared texture/cache/fence ownership |
| Full cross-backend/CTest/sanitizer | Not run | single final user-requested native Mac pass |

## Next Stage 164

Prioritize child animation Graph restore-status instrumentation and persist host-owned parameter overrides (not procedural hidden state) across reload where authored names/types are compatible. Then continue complete rig/LOD/morph closure and GPU resource lifetime before one coordinated full native test run. Create one stage handoff for each change; update `docs/agent-handoffs/animation-universal.md` and provide a user-visible readiness table.
