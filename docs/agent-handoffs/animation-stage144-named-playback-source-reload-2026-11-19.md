# AIVERSE MotionWeave / Universal View — Stage 144 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 143](animation-stage143-geometry-world-streamed-model-retirement-2026-11-19.md)

## User workflow and branch safety

The user explicitly requested: **finish all animation / Universal View functions first, then execute the entire test suite in one coordinated pass**. This is a feature-only implementation stage; NO native CMake/compiler/CTest/GPU/sanitizer/benchmark or fresh passing CI was executed. Do not claim tests passed, the features are all complete, or PR is mergeable. `tools/animation/validate-final.sh` remains the eventual one-command Mac runner after genuine feature freeze.

Continued Stage 143 HEAD `4041be1df74e9018f1a9a2560d45df0633d1668d`. Last source implementation HEAD `b9b5e7143bea5fadc0b5db0ca52790a567282d33`.

At implementation readback, the animation feature branch was **ahead 1121 / behind 467** versus `main`, status DIVERGED, merge base `d71c6d47e532b25fb816a8bb1f96c41e5275a165`. Against PR #22 base `feature/universal-entity-inspector-fps`, feature ahead **1491 / behind 615**, status DIVERGED, merge base `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. These are point-in-time snapshots; current main has advanced while multiple other agents are editing renderer/texture/geometry/world systems. NO rebase, force-push, merge, PR retarget or main/peer branch edits. Review with respective owners in a separate disposable integration worktree before any merge.

## Problem and general architecture

`AnimationGraphInstance` already had `snapshotPlayback()` and `restorePlayback(snapshot)`, but authoring and generic presentation-adapter hot reload frequently called `reset(newProgram)`, restarting named animation states and clips even when old and new graphs were compatible. On a Model source reimport, `RtsLook::reloadModelSources` erased affected per-entity `EntityAnim` state to prevent stale borrowed Model/Graph/skin references — correct lifetime behavior, but it also discarded harmless presentation clip time and made an existing object behave like a new spawn. Streaming coarse/detail Model choice refresh similarly erased clocks.

Stage 144 preserves **ONLY portable, named and bounded presentation data**. It does NOT preserve old Model pointers, bones, rig/IK/contact caches, native operation state, graph pose matrices, crossfade state, unconsumed notifies or authoritative simulation/root motion. Incompatible graph/state/clip identity continues from new graph defaults. The actual MotionWeave ECS source bridge still owns its independent animation clock; it must not inherit a stale legacy clock.

## Exact source commits

1. `f67fa2abaf065ed7255f5142f74308db47545537` — `engine/render/AnimationGraph.h`: public, genre-independent `AnimationGraphInstance::rebindPlayback(shared_ptr<const AnimationGraph>)`.
2. `ee0d0eaa37d5874d4add1b5aa3ccbc890e377ffd` — `engine/render/AnimationGraph.cpp`: snapshot old named state/clip phases if healthy, `reset` to the new graph, conditionally `restorePlayback`. Same-program identity is a no-op. The previous graph's native solver, pointers and event queues never survive.
3. `9548804f8785200e4b1d0dd9d85e299ce5292d50` — `modules/rts/view/RtsAnimate.cpp`: when a programmable or Lua graph pointer changes, use generic `rebindPlayback` for the legacy animation owner instead of always restarting. The separate generic ECS owner remains independent.
4. `7d0314866a8f78d82d659651f1a59486e41ced93` — `apps/editor/animation/AnimationEditor.h`: preserve named phase on graph-only edits, and copy user-edited control values/vectors only for parameters still present in the new program. Never carry stale unknown parameters.
5. `8434123962fa8950494d407130c7497848bd0ffd` — `modules/rts/view/RtsView.h`: bounded `ReloadPlayback` value snapshot registry keyed by full entity ID, independent from GPU/Model ownership.
6. `2cb9c20bf98559299fe443ca318bef82f5461a96` — `modules/rts/view/RtsLook.cpp`: capture portable snapshot before erasing affected `EntityAnim` entries on model source reload, authored graph/material appearance reload, or streaming coarse/detail Model choice replacement.
7. `d0caeda405138d35ba91059730a0e05cf655c9cd` — `RtsAnimate.cpp`: on first presentation frame after a compatible reload, restore event ages, named graph clock, presentation phase, speed/yaw/position/death state; protect entity generation/type/choice identity and consume one-shot snapshot without injecting a duplicate spawn event.
8. `b6b7fa91ebeaf8eef0cf501336f4f3e3494d3d83` — `RtsView.cpp`: clear pending clock snapshots when a new project/device session initializes or shuts down.
9. `edc49a387da84cfb7af0a683ea96cc5f44defcc0` — `apps/editor/windows/ModelingWindow.cpp`: restore compatible named clock into the **newly compiled/new Model** preview graph before old Model retirement; retain current preview time when playback is active and recovery succeeds, otherwise reset.
10. `5e81cf976577fcbb456fc57e9461f39a72e0095a` — `AnimationEditor.h`: capture an old portable clock before replacing the editor preview Model, then attempt restore after compiling the replacement model's graph. Graph-only reload uses `rebindPlayback`.
11. `e88ff9e9da3cda7da81df89442046a1cfcc33d71` — `RtsLook.cpp`: prune stale registry entries when reaching 8192 pending snapshots; expire entries older than 120 View frames.
12. `1aec7cabc91339a43b0ca96f6ad566c73e887444` — `RtsAnimate.cpp`: avoid inheriting stale legacy clock when generic View presentation was the previous animation owner and legacy is being reactivated; that path still uses an explicit fresh owner reset.
13. `03d0d1e34d2fbc823c185606b1972190728d6959` — `RtsView.h`: declare optional snapshot capture `noexcept`.
14. `063e7265044bc4dbb4ac54985c1c17def9a4d73d` — `RtsLook.cpp`: contain allocation errors while capturing purely cosmetic snapshots, so source GPU publication/invalidation cannot fail only because the optional clock registry was out of memory.
15. `748c265e9eb7c570df0296ecd30085e2fe3de681` — `RtsAnimate.cpp`: contain optional restoration allocation errors; a new valid graph falls back to default clocks.
16. `6842ab0dc6df083735e17d83b8f39a159a70cf47` — `RtsView.h`: add per-entity pending portable graph snapshot; final restoration MUST wait for per-entity programmable/Lua graph selection, not occur prematurely against a temporary default TypeLook program.
17. `bf7511a042e342ae02f526e3da207c75a8f27bb9` — `RtsAnimate.cpp`: perform one-time portable restore after the final compatible program is selected, and never restore the legacy state into an independent generic ECS graph player.
18. `96ddcf3621313003f2835325f6557a266fc906a0` — `RtsView.h`: snapshot of an optional *legacy Animator-only* base-layer clip by name, normalized phase, loop and semantic animation kind (no raw index).
19. `35fc55d5fa4ce649b75e6870c754e62595ca7029` — `RtsLook.cpp`: capture only an unblended, finite, known base-layer clip; do not preserve cross-fades or other layers from old models.
20. `b9b5e7143bea5fadc0b5db0ca52790a567282d33` — `RtsAnimate.cpp`: restore a legacy base clip only when its named clip and semantic kind still exist and the loop mode matches, then `Animator::seek()` to normalized phase. All old Model clip indices, masks, bones, additive/IK and notifies remain discarded.

No CMake test registration or actual native tests were changed/run in this user-requested feature-first stage.

## Detailed API and source identity behavior

### Generic AnimationGraphInstance

`bool AnimationGraphInstance::rebindPlayback(std::shared_ptr<const AnimationGraph> next)` is engine-level, genre-neutral and portable to any native project. It attempts old `snapshotPlayback()` only for a valid healthy graph, calls `reset(next)`, then uses the existing validated `restorePlayback`. Returns true only when the prior state/clip identity can be mapped; false means the new graph is valid but restarted. A new graph must not contain old Model-owned pointers. Named phase transfer does not generate prior notifies, or restore untrusted rig/IK/native operation state.

### RTS source/LOD/graph reload

`RtsView::ReloadPlayback` is a Model-free value object:
- Full `uint64_t` entity ID (stored as the map key), unit type, visual choice, captured View frame.
- Named state/clip snapshot, optional unblended legacy Animator base clip, normalized phase, loop and animation kind.
- Presentation-only age clocks (attack/hit/spawn/death), cast age, death state/age, phase, yaw, speed and last scene position.

Maximum **8192** pending entities. A snapshot is consumed at the first matching `stateFor` and cannot be applied after **120 View frames** or to a new type/entity generation. Under registry pressure, old snapshots are removed first; if still full, optional capture is skipped. Both capture and restore fail closed to a clean new animation on memory error. A valid same-entity rebind suppresses spurious new-spawn presentation events.

Since entity-specific Lua/programmable graph selection happens later than `stateFor`, the portable graph clock first moves into `EntityAnim::pendingReloadPlayback`. It is restored **after final program selection**, not against an intermediate default graph that might differ. Generic View ECS animation remains the sole owner of its own sources; legacy snapshots never override its state.

Legacy clip-only actors restore a base-layer clip only when the next Model has the SAME named clip, that clip still belongs to the same authored base animation kind and the loop mode matches. This specifically does NOT preserve blended walk/run pairs, fade stacks, masks, additive layers, fired clip events or contact state.

### Editor authoring

Both Modeling and Graph/Rig preview can keep named compatible phase during a graph or Model update without keeping old GPU/Model pointers. Modeling preview prepares next graph and pose prior to Model publication and applies the named clock while the old graph is still alive; graphical preview `time_` retains its value only for compatible active playback. AnimationEditor's new-Model path saves a portable snapshot before its Model change releases the previous graph, restores after next graph compile, and retains matching parameter values across graph-only reloads.

No authoritative gameplay tick, network/save state, engine GPU retirement token, renderer shader cache, main Device epoch, or file/schema format changed.

## Actual verification and not-run tests

**ACTUALLY PERFORMED:** read via connected GitHub the generic AnimationGraph header/implementation and existing `snapshotPlayback`/`restorePlayback` semantics; `RtsAnimate`, `RtsLook`, `RtsView.h/.cpp` and authoring widgets; source-level readback of new methods, all three reload capture paths, pending-per-entity final graph restore and legacy clip mapping; current branch/main/PR-base comparison. SHA-guarded source commits were written and read back successfully. No local Mac worktree available.

**NOT RUN:** local compiler/CMake/CTest, a green CI job for this head, macOS Metal/GL/Vulkan, studio/editor/FPS/RTS visual parity, hot-reload failure injection, root-motion/notifies/no-double-writer tests, ASan/TSan/UBSan, performance or `tools/animation/validate-final.sh`. No test pass or production-readiness claim.

## Remaining feature gaps and next Stage 145

1. The **genre-neutral `MotionWeaveSourceBridge` presentation ECS world** can currently restart on Model publication epoch or graph identity changes. Audit its actual ECS `AnimationGraphInstance` and model asset component ownership; implement its independent named-phase transfer only when logical entity generation is unchanged and the new Model epoch/rig is validated. Never copy native solver/root-motion state or allow two writers.
2. Continue Universal View migration beyond static and simple animated packets, with full native FPS/RTS/editor graph/LOD/attachment presentation parity, source publication generations and cancellation of stale async model results.
3. Verify animation graph/retargeting authorship with `AppearanceAssetRegistry` and legacy choice adapters; do not infer full feature completion solely from named playback restoration.
4. Coordinate with concurrently advancing renderer/texture/geometry/world agents. `main` and the separate PR base remain hugely divergent. Use a reviewed disposable integration worktree; no automatic rebase/force push.
5. Keep a standalone Stage 145 handoff and append-only `AGENTS.md`, `docs/animation-graphs.md`, and `docs/agent-handoffs/animation-universal.md`. Only after genuinely completing the full feature-closure gate run the complete native Mac suite with `bash tools/animation/validate-final.sh`, plus real GPU visual/fence validation. Keep PR #22 Draft until verified.
