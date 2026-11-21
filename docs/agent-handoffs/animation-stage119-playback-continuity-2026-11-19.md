# AIVERSE MotionWeave / Universal View — Stage 119 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open, Draft**)
Previous: [Stage 118](animation-stage118-gpu-init-rollback-2026-11-19.md)

## Git status and ownership

- Started at Stage 118 HEAD `ca16117e34a9a662f7fb3d586d52ae51670a91f7`.
- Initial connected GitHub compare: main
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`,
  animation branch ahead 785 / behind **0**.
- Existing main integration is already on this branch; no new merge,
  force push or main update performed. Refresh ancestry before the next
  stage because parallel agents modify other branches.
- Own only the generic presentation/animation View bridge and
  Renderer3D fallback; do not mix in FPS/RTS gameplay, PHYSIS,
  NEREID water dynamics, SpectraFX or Prisma ownership.

## Implemented: compatible MotionWeave hot-reload playback migration

1. `engine/render/AnimationGraph.h`,
   commit `d35609ec566973554d46da20bce454227a40c73d`:
   optional `AnimationGraphInstance::PlaybackSnapshot`
   (current named state, state elapsed time, named clip
   identity/phase/loop) and
   `snapshotPlayback()/restorePlayback()` APIs.
2. `engine/render/AnimationGraph.cpp`,
   commit `d2453cfcfd8de11c3650ce28c9b22999d98fe2d4`:
   capture clock phase normalized to each clip duration;
   restore only a matching named graph state, matching node/clip
   names, loop semantics and finite phases. Adjust for changed
   clip duration. No phantom time-zero notifies. Rejection leaves
   the newly reset player at authored defaults. Never copy
   old local pose, IK/contact/natives, GPU buffers or gameplay state.
3. `libraries/include/aiverselib/animation/EcsAnimationSystems.h`,
   commit `41039f8f1490eac62ecfced461a65b63329d16ce`:
   presentation-only `AnimationPlayerComponent::pendingPlayback`
   is consumed exactly once after `reset(newGraph)`, prior to the
   current frame's update/evaluate. It is discarded on missing
   resources, and a new pose is evaluated for the new Model.
4. `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`,
   commit `fa2d0675438eeebbb7367e24caf178adeaa18c83`:
   on model/graph pointer changes of the SAME source generation,
   capture the previous healthy graph's playback only if old/new
   node names and parent hierarchy match. Keep the existing
   entity and authored inputs; restart native/IK/montage/notify
   state. A changed source generation (respawn), reordered bones
   or renamed ancestry intentionally gets a full animation restart.
   The host must keep old Model/Graph alive until source sync
   completes; this does not provide deferred-asset ownership.

### Compatibility limits

- This is an optional migration for the **universal** MotionWeave
  presentation World only. Legacy game-specific animation draw paths
  not yet migrated to the bridge remain unchanged.
- Name-based phase/state migration is presentation-only; it does NOT
  snapshot internal motion matching, native callback state, montage
  section progress, transition fading, contact IK, physical simulation,
  unwrapped notify cycles or full rig pose. Reload may still have a
  one-frame pose discontinuity when the new graph changes structurally.
- A compatible skeleton check compares node names and parent indices
  in the same order; intentionally does not assume retargeting.
  Skinned attachment meshes with distinct skeletons require explicit
  host adaptation; ordinary host-supplied attachments and LOD draws
  still use `View::submitModel`.
- An old compiled AnimationGraph must not outlive its source Model
  during asset replacement; preserve borrowed lifetimes through
  `MotionWeaveSourceBridge::sync` and finish worker jobs first.

## Implemented: renderer fallback GPU resource safety

5. `engine/render/Renderer3D.h`,
   commit `88dfdf4bbab681f58cd912417f50bfcbbb261429`:
   track unleased fallback target orphans and idle-barrier failures;
   expose `unleasedTargetIdleFailures()`. Existing
   `targetRetirementOrphanPairs()` includes unleased orphans.
6. `engine/render/Renderer3D.cpp`,
   commit `d1893fc521bafcbc6a59d5f06289b82f4c4fd75e`:
   replace direct fallback texture deletion when a shared
   `OffscreenTargetRetirement` is absent with a proven
   `Device::waitForResourceIdle()` check. If no Device, unsupported
   idle, error or exception, retain/orphan the GPU handle instead
   of releasing resources that earlier submissions could still use.
   The renderer-owned queued/fenced path remains unchanged.
7. `apps/editor/windows/StatsWindow.cpp`,
   commit `0c816239fcbfe9865f9633c2267d276f40accf9c`:
   expose fallback idle failures in the GPU diagnostic panel.

## Additional neighboring owner audit

- `engine/render/RenderTexture.cpp` currently destroys GPU draw
  vertex buffers directly after `endPass`, destroys temporary
  upload textures immediately after blit, and immediately frees its
  persistent canvas/shader/pipeline on shutdown. It has no observed
  GPU fence/idle policy. **Not changed in this Stage**: it needs a
  separate owner-aware transient buffer/texture retirement refactor,
  preserving the canvas upload order.
- `engine/render/PostFx.cpp` uses fenced RTT ownership when leased
  but has a legacy immediate-destroy path without a lease.
  Stage 115 documented this compatibility mode.
- SpectraFX, NEREID, terrain and standalone renderer use other
  owners; do not infer identical lifetime rules without inspection,
  and do not overwrite parallel teams' code.

## Validation ACTUALLY performed

- Connected GitHub repository / branch / PR reads, source inspection
  for `RenderTexture`, PostFx, MotionWeave graph/ECS/View and renderer
  lifetimes, branch-targeted blob-SHA edits.
- **NOT RUN:** native clone, CMake configure, C++ build/link, CTest,
  CI, GPU Metal/GL/Vulkan rendering, FBO/RTT lifecycle fault injection,
  screenshots, anim hot reload benchmark, ASan/TSan/UBSan. All
  Stage 119 functional changes remain **UNVERIFIED** for compilation
  and runtime; PR #22 remains Draft.
- Deferred cases: duration-shifted clip phases, compatible graph
  recompilation, model pointer changed, clip/node identity changes,
  generation respawn reset, notify suppression, hierarchy mismatch,
  old Model lifetime, editor/RTS/FPS visual parity, idle failure and
  RenderTexture buffer/temporary image use-after-free.

## Exact next Stage 120

1. Fetch latest main and the animation feature branch before edits:
   `git fetch origin main feature/universal-ecs-animation-pipeline`
   then `git switch feature/universal-ecs-animation-pipeline`
   and `git pull --ff-only origin feature/universal-ecs-animation-pipeline`.
   Check commits and preserve concurrent changes.
2. Introduce safe GPU transient buffer/texture retirement for
   `RenderTexture` (canvas brush, blit and upload). No per-brush
   full-device idle or direct post-submit destruction; use
   one device-owner fence queue and safe host frame boundaries.
   Continue remaining unleased PostFx/terrain/FX audits with the
   responsible agents.
3. Finish any missing View model/LOD/attachment host migration;
   keep one graph update per frame and avoid duplicate source IDs.
4. Add deferred regression sources for clips/montages/hot reload and
   GPU lifetime failures; **features first**, then execute native
   build/CTest/sanitizer and real GPU visual/benchmark tests, fix
   warnings and only then consider merging PR #22.
5. Add a separate Stage 120 handoff and update `AGENTS.md`.
