# AIVERSE MotionWeave / Universal View — Stage 148 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT, NOT MERGED at source-readback
Previous: [Stage 147](animation-stage147-rts-publication-epoch-and-graph-rebind-2026-11-19.md)

## User's binding architectural rule

**Animation is completely independent of game genre.** `MotionWeave`, reusable graph/pose libraries and the canonical `aiverse::View` must not import, depend on, interpret or branch on FPS, RTS, RPG, networking, weapons, gameplay actions or any other genre. Each host owns its own source projection (logical ID + generation, immutable Model, published Model epoch, compiled AnimationGraph, authored parameter names and cosmetic cues). Host-specific events and simulation state never enter animation core. There is exactly one presentation animation writer for any given visual source; generic scene submission must not start a second evaluator.

The user requests **all animation features first, then a single coordinated full native test pass**. This stage contains source changes and connected-GitHub readback only. No native build/CTest/GPU benchmark or fresh passing CI was run. Keep PR #22 draft.

## Actual code commits and paths

1. `4afaeb241d089b424cdce1284e292c92d4847c66` — `libraries/include/aiverselib/animation/EcsAnimationSystems.h`: new universal `AnimationGraphRebindResult` and `rebindAnimationPresentationGraph(World&, Entity, const render::Model&, shared_ptr<const AnimationGraph>)`.
2. `e2d29208298a0c93f10917e5a5026a1db01c2351` — `apps/fps/client.cpp`: the host now calls the above **genre-neutral** graph-only rebind helper instead of duplicating the low-level graph snapshot/restart, montage/notifies and player state mutation. The host still owns player slots, gameplay event mapping, clip choices and backend selection; NONE of that enters MotionWeave.
3. `97eed17f7efdb6eb8e8ad3efd231e4a72218f3f9` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`: require a **changed compiled Graph identity** (address inequality, comparison only) for opt-in pre-detach SAME immutable Model + SAME publication epoch handoff. Tighten healthy graph instance eligibility for in-place ordinary `sync` changes.
4. `0ab341378050e21c796f4d20b999c87e189e99e1` — same source bridge: synchronously invalidate `AnimationPoseComponent::valid` on source generation, model epoch, model pointer or compiled graph change, **before** any animation job can be skipped or any old pose can be submitted. New pose matrices are computed only by the normal single MotionWeave writer.

## Universal graph rebind API and behavior

```cpp
// Presentation-owner thread, after animation workers join, while old CPU
// Model + graph remain readable; no simulation or renderer call.
auto status = aiverse::motionweave::rebindAnimationPresentationGraph(
    presentationWorld, visualEntity, immutableModel, newlyCompiledGraph);
```

For a living ECS source with an existing `AnimationAssetComponent` bound to the SAME immutable Model and a newly compiled graph whose `graph->model()==&model`, this performs a graph-only rebind without adding/destroying ECS entities. It returns `AnimationGraphRebindResult {replaced, playbackCaptured}` (captured means a portable named snapshot was **accepted**, not that a later GraphInstance restore succeeded). Unchanged graph pointer or missing/mismatched ECS asset/player returns `{false,false}`. The helper:

- Discards any earlier pending seed and *optionally* captures current named state/clip phases from a healthy, nonrestarting old Graph player. Optional capture exceptions are swallowed so a valid graph publication can continue at clean defaults.
- Installs the new compiled Graph and marks the existing AnimationPlayer for exactly one normal update/restart. Only the normal MotionWeave ECS evaluation updates animation; no extra graph evaluator, no gameplay changes.
- Marks prior pose invalid immediately, discards stale montage players/events and graph-notify queue; old contact/rig/native/root-motion state is NOT copied. The existing ECS player initialization takes care of resetting its recovery and source-specific evaluator data at the next update.

The method is **only for graph-only edits while the existing immutable Model survives**. A full Model source replacement, CPU retirement, LOD asset swap or different skeleton uses the pre-retirement `forgetForReimport`/source bridge lifecycle of Stages 145–147, not this helper. Hosts still own ModelReadScope/GPU lifetime tokens and cannot call this method during worker dispatch.

## Native host audit: actual owner roles

- The reference `apps/fps/client.cpp` has a dedicated **presentation-only** World and MotionWeave graph/crowd scheduler. It maps its network/game actions to graph parameter names and cosmetic montage cues in its host. It now uses the generic graph-rebind helper for same-Model authored graph hot reload. Remote precomputed pose or baked sample draws go through `genericSceneView.beginPreparedFrame()` + ordinary `View::submitPackets`, without calling `View::update` on those already-evaluated objects. `packet.id=remote.slot`, `packet.generation=remote.life`, and `animationModelGeneration` is sourced from `characterAppearance->modelPublicationGeneration`; no additional player should be created in View to avoid double writers.
- `apps/editor/windows/ModelingWindow.cpp` and `apps/editor/animation/AnimationEditor.h` own independent **editor preview** `render::AnimationGraphInstance` state, rather than an ECS presentation World. Both already use portable named playback for compatible preview Model/Graph reloads (`nextGraph.restorePlayback` and `instance_.rebindPlayback` respectively). Do NOT allocate a second MotionWeave ECS player just for those preview panels. The editor's GPU retirement owner remains responsible for old Model pointers.
- `modules/rts/view/RtsView.cpp` projects only eligible sources into generic `aiverse::View`; legacy complex procedural/IK/attachments stay in its adapter until parity. Its source packets now carry the nonzero `ModelChoice::modelGeneration` and stable packed ECS IDs from Stage 147. A genre-specific adapter is permitted in `modules/rts`; engine View / MotionWeave must NEVER depend on it.

## Safety and compatibility limitations

- The Stage 146/147 pending pre-retirement record now also retains the **numeric-only old compiled Graph address** as a conservative identity guard; it is never dereferenced, never owns Graph/Model lifetime and may yield a safe **false negative** if an allocator reuses an address. It cannot prove host mutation correctness. SAME immutable Model + SAME nonzero publication epoch is eligible only when the newly bound Graph has a different address. A new Model still requires a genuinely different address and publication epoch; unchanged logical generation and exact copied rig name/parent hierarchy are mandatory. Other mixed address/epoch combinations restart.
- `MotionWeaveSourceBridge::sync()` now invalidates old `AnimationPoseComponent::valid` synchronously on ANY asset or source-generation replacement. If dt is invalid, workers skip or a frame suspends, the renderer cannot submit stale bone pose data. No change to simulation ECS, network/save state, native animation authoring schema, GPU model allocation/retirement, or `AnimationSourceFrame` aggregate field order.
- The reference FPS host does not currently use `MotionWeaveSourceBridge` for its own direct presentation World and does not require pre-retirement `forget` on graph-only edits. Other projects may choose the bridge or host-owned ECS player directly, but **only one owner may evaluate a source**. The host must still guarantee old compiled Graph/Model CPU lifetime while capturing.
- `AnimationGraphRebindResult::playbackCaptured` intentionally makes no assertion about eventual `restorePlayback` success. Incompatible named state/clip, missing player, or capture exceptions mean clean restart.

## Verification performed and NOT RUN

**ACTUAL:** connected GitHub source read for Stage 147 handoff; core source bridge, ECS AnimationSystems, canonical MotionWeave namespace export, generic View, full relevant FPS animation reload and precomputed render path, editor Graph/Rig and Modeling preview. Confirmed native FPS used separate legitimate MotionWeave owner and duplicated only low-level graph swap. Exact four code commits were created using fetched blob SHA guards, and readback verified the new helper, host call, source-bridge guards and immediate pose invalidation. Checked PR #22 is open Draft and unmerged at Stage 148 code HEAD `0ab341378050e21c796f4d20b999c87e189e99e1`.

**NOT RUN:** local CMake/C++ compiler/link, CTest, green CI for new head, Metal/GL/Vulkan GPU rendering, interactive FPS/RTS/editor parity, resource-fence fault injection, ASan/TSan/UBSan, large-crowd allocation/perf, final `bash tools/animation/validate-final.sh`. Do not claim implementation is production-ready or feature-complete.

## Next Stage 149

1. Continue truly universal animation features: review imported authoring pipeline, retarget/LOD/attachments for a **single** reusable API from any native project; no genre branching in animation libraries.
2. Audit generic source identity contracts for anonymous (zero) source IDs, source generation vs asset epochs and cross-`forget` record expiry/clear; ensure misidentified identities cannot reuse stale playback. Keep compatibility and source ownership explicit.
3. Add or stage final tests for common graph-only rebind: previous named phase survives, renamed state restarts, invalid/missing model is rejected, capture exceptions are contained, montage/notifies and old pose invalidated before worker update; plus source-bridge graph identity, epoch/rig mismatch, stale-worker and playback diagnostics. Run only once after feature freeze per user's workflow.
4. The histories of `main` and draft PR base were already divergent during Stage 145–147. Coordinate final integration with peers in a disposable worktree. No auto-merge/rebase/force push or edits to other agents' branches.
5. Commit a separate Stage 149 handoff and append-only active journal after any further implementation. After actual functional closure run the full native Mac validation runner and real GPU visual proof, fix failures, then reassess Draft PR readiness.
