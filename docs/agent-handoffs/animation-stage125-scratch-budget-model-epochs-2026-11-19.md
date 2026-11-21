# AIVERSE MotionWeave / Universal View — Stage 125 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 124](animation-stage124-pose-scratch-authoring-2026-11-19.md)

## Repository state and ownership

- Continued Stage 124 HEAD `9d3f38dc721da8cdfe0cffa28674529798ed3dc5`.
- At start, connected GitHub compared feature with `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: 894
  commits ahead, 0 behind. After Stage 125 FUNCTIONAL edits: ahead
  907, 0 behind, HEAD `3be01b3bc4544125a9517fa4229e24bfc83639fb`.
- Changed the SAME `feature/universal-ecs-animation-pipeline` only.
  No force push, rebase or main mutation; main was already an ancestor.
  PR #22's GitHub base remains `feature/universal-entity-inspector-fps`,
  not `main`; verify PR base explicitly before eventual merge.
- Scope: universal View scratch-retention budget and model publication
  revisions; optional RTS compatibility adapter policy, editor stats.
  No simulation/gameplay authority, PHYSIS, NEREID, TerraWeave,
  Prisma, SpectraFX or rendering GPU-device ownership was moved.

## Source commits and changed files

1. `f39ac9a6fdb005493fbcf58a1b252eb2937d99ed` —
   `engine/view/View.h`:
   - Default *retained scratch capacity budget* = 512 KiB PER View.
   - `setRetargetScratchBudgetBytes(size_t)`,
     `retargetScratchBudgetBytes()`,
     `retargetScratchCapacityBytes()` public APIs.
     Budget may be zero (release retention) or configured by native host.
   - `RetargetScratch` remains per-View and owner-thread-only. The
     RAII `ReleaseScratch` checks capacity at the END of each
     synchronous `submitOne`, after Renderer3D and any capture callbacks
     finished reading its pointers. If the cumulative retained Pose/mat4
     capacity exceeds the limit, clears and frees both vectors.
   - A large actor IS NOT denied or truncated: memory above the budget
     can exist during its draw; only long-lived retained capacity
     is bounded afterward. Nested capture submissions already use
     isolated stack scratch and do not overwrite the outer vectors.
   - `ModelRetargetStats::scratchTrims` records trimming per frame.
     `View::clear` and budget changes reject calls during active nested
     rendering. No gameplay clocks or MotionWeave ECS jobs are touched.
2. `99a6ac125b543acb8e99ad6579408f7976870b40` —
   NEW `engine/view/ModelPublicationEpoch.h`:
   - Genre-independent, process-wide atomic monotonic nonzero
     `ModelPublicationEpoch::next()`. Uses checked CAS: no zero or
     wrapping revision, throws on 64-bit exhaustion. Host increments
     when publishing a replaced/re-imported Model even if C++ pointer
     address is reused. This is a *CPU asset identity*, not an ECS
     entity generation or GPU completion fence.
3. `eb4d92ae729fb630a54922ebe58f091300d5f64a` —
   `modules/rts/view/RtsLook.cpp`: removes its private per-module
   publication atomic and uses `ModelPublicationEpoch::next()`
   for new ModelChoice versions. Retarget plans remain compiled at
   load/publication, not at every character draw.
4. `78c5e6fb3410666601db9b3e291bdc91881c51b3` —
   `engine/view/AppearanceAssetRegistry.h`: successful
   `publish` and `publishPrepared` assign each new
   `Asset::modelPublicationGeneration` via the global revision
   helper; `Asset::packet` forwards it to the emitted
   `view::Object::animationModelGeneration`. This also invalidates
   any old plan after a material-only appearance re-publication;
   hosts must rebuild an opt-in compiled LOD plan using the
   currently published generation. Old graph/source generation
   behavior remains independent.
5. `4157c35e7086d3eb00a06b10b1a16c73ff9def0c` —
   `engine/view/ModelPoseRetarget.h`:
   `CompiledRetargetStatus` distinguishes missing source/visual
   version, changed source/visual pointer, changed source/visual
   version, invalid map shape, and Ready. The plan's `status`
   compares pointers/versions in O(1) without dereferencing stale
   pointers. Existing `matches` delegates to status.
6. `4b1eb207724b09157535893e94968344fb1b5079` —
   `engine/view/View.h`: `ModelRetargetStats` additionally
   classifies `compiledMissingGeneration`,
   `compiledExpiredGeneration`, `compiledModelMismatch`,
   `compiledInvalidPoseOrShape`, on actual rejected compiled
   submissions. On any reject, visual Model uses bind pose.
7. `8988e4915c9228e31518e1acb8fff90a5768919a` —
   `modules/rts/view/RtsView.h`: opt-in
   `LegacyLodFallbackMode { PreserveWorldMatrices, BindPose }`,
   setter/getter and `LegacyLodFallbackStats` counters.
   Default remains `PreserveWorldMatrices` for existing authored
   RTS projects until native visual parity confirms migration.
8. `b71058d362f6ebc9d0d0a4405dfbb22d74b99ff1` —
   `modules/rts/view/RtsViewDraw.cpp`: without an accepted
   compiled plan, choose the historical node-matrix mapping
   (default) or strict bind-pose fallback (opt-in). A REJECTED
   previously compiled plan always falls back safely to bind
   pose, regardless of configured legacy mode. Counts each
   legacy copy, bind-pose fallback and compiled rejection.
9. `f23bd9ea257d69cc6be7d50f8f9e4fd7979b16ad` —
   `apps/editor/windows/StatsWindow.cpp`: exposes an editor
   checkbox for strict RTS LOD bind-pose fallback versus old
   visual behavior, plus lifetime fallback counters. This is
   a diagnostic/preview control, not a gameplay setting.
10. `7f62a3c23e5501fc13db357e127581eb19117004` —
    `modules/rts/view/RtsView.h`: declares host scratch budget setter
    and capacity accessor for its lazily constructed generic View.
11. `d77d4ecaa57a1df4ab3f26472867531b7b2d80fe` —
    `modules/rts/view/RtsView.cpp`: forwards budget changes
    to live `genericView_` and applies the chosen budget upon
    future View construction. It never starts a second clock.
12. `007bf73ebe4fa7d86e15030c50a8494032a87c4e` —
    `RtsView.h`: read-only scratch budget accessor for diagnostics.
13. `3be01b3bc4544125a9517fa4229e24bfc83639fb` —
    `StatsWindow.cpp`: reports retained MotionWeave scratch
    capacity vs configured budget in KiB.

## API contracts and native C++ usage

```cpp
aiverse::View view;
view.setRetargetScratchBudgetBytes(256u * 1024u); // 256 KiB retention
// A single character with a much larger rig may still render.
// The extra cached capacity is returned AFTER synchronous submission.
auto used = view.retargetScratchCapacityBytes();
auto budget = view.retargetScratchBudgetBytes();
auto stats = view.modelRetargetStats(); // includes scratchTrims

// At an asset-publication boundary:
const uint64_t modelEpoch =
    aiverse::view::ModelPublicationEpoch::next();
aiverse::view::Object packet;
packet.animationModel = &ownedModel;
packet.animationModelGeneration = modelEpoch;
// Compile retarget plans using source + visual publication epochs.
// Rebuild them on every replacement, even when a C++ address is reused.
```

- Scratch limit is *retention*, not a hard cap on active draw memory.
  A release is allowed only when the synchronous draw has completed.
  Never invoke a View's update, clear, budget setter or a concurrent
  submit on the same instance from a worker/capture callback.
- The global Model publication generation does not manage CPU/GPU
  lifetime. Host must keep borrowed Model/Graph/retarget memory valid
  through the presentation worker barrier and Renderer3D submit.
  GPU buffers/images still require the separate fenced owners.
- `AppearanceAssetRegistry` assigns a new global revision even
  on a material-only publication. This is deliberately
  conservative for safety; compiled LOD maps may have to be
  regenerated, even if the underlying Model bytes did not change.
- In an RTS adapter, opt into strict compatibility mode with
  `view.setLegacyLodFallbackMode(
  game::RtsView::LegacyLodFallbackMode::BindPose)`. The Editor
  Statistics window exposes the same switch for visual comparison.
  Normal compiled LOD maps are unaffected by this toggle.
- No second MotionWeave source clock or simulation writer was added.

## ACTUAL validation, NOT ASSUMED validation

ACTUALLY performed in this ChatGPT turn:
- Connected GitHub PR/branch/ancestry read, current file SHA reads,
  targeted source updates, and re-read of relevant GPU-renderer
  pointer-lifetime contracts from the previous Stage 124 handoff.
- Source-level inspection only for View, ModelPoseRetarget,
  AppearanceAssetRegistry, RtsView/RtsLook/RTS draw and Editor stats.

**NOT RUN:** local checkout, native CMake configure, C++ compile/link,
CTest, GitHub Actions CI, Metal/Vulkan/GL rendering, visual parity
screenshots, fault injection for OOM/capture reentrancy, sanitizers,
memory profiling or performance benchmarks. THIS CODE IS UNVERIFIED
FOR NATIVE COMPILATION OR RUNTIME CORRECTNESS.
Keep PR #22 OPEN / DRAFT. No measured speedup claimed.

## Known limitations / next Stage 126

1. Retarget scratch has a retention budget, not a strict instantaneous
   allocation budget. Other renderer, graph and skinning allocations
   are not included. Large rigs may allocate beyond the budget
   transiently and may repeatedly allocate each frame if the budget
   is too low. Measure real workloads before tuning defaults.
2. `View::modelRetargetStats` counts only generic View submissions;
   legacy RTS direct draw uses separate cumulative counters.
3. Appearance registry re-publication changes a revision even if
   the exact Model storage is reused; that is safe but may cause
   unnecessary opt-in plan rebuilding. Investigate finer-grained
   model-specific generations only with reliable host ownership.
4. The RTS Legacy world-matrix LOD fallback remains enabled by
   default; strict bind mode is opt-in. Visual comparison and
   screenshots are required before deciding a new default.
5. The new Editor checkbox lives on the Stats panel and does not
   persist as a project gameplay setting.
6. Need source/visual Model epoch diagnostics in other native host
   (non-RTS) authoring UIs, and explicit twist/reduced-bone retarget
   profiles if project visuals require them. A single generic
   name-map is not a high-quality retarget solver.
7. Next Stage 126: finish any remaining universal View/model
   compatibility migration and native asset publication
   handoffs; finish ALL features, THEN run full CMake,
   CTest, Metal/Vulkan/GL FPS/RTS/editor visual parity,
   ASan/TSan, memory/fault injection and benchmarks,
   fix compiler errors and warnings. Produce standalone
   Stage 126 handoff plus `AGENTS.md` update.
