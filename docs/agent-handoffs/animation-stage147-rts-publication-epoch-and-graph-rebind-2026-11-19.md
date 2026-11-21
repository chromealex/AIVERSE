# AIVERSE MotionWeave / Universal View — Stage 147 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT (not merged)
Previous: [Stage 146](animation-stage146-pre-retirement-playback-rebind-2026-11-19.md)

## Objective

Make the Stage 146 opt-in pre-retirement named playback handoff functional for the existing RTS `RtsView` adapter, not merely exposed as a generic API. Audit found that although `RtsLook.cpp::loadChoice` already assigns every published `ModelChoice::modelGeneration` from the nonzero monotonic `view::ModelPublicationEpoch::next()`, the `View::Object` packets sent to generic MotionWeave omitted `animationModelGeneration`, so it defaulted to zero. Stage 146 correctly rejected all cross-detach phase restoration in this host. A second gap concerned graph-only authoring reload: the immutable CPU Model and epoch may remain unchanged while its compiled AnimationGraph is replaced.

## Exact code SHAs and source paths

1. `69b5d2e1f783caa7d1408b6881e8eafa7ff81d4f` — `modules/rts/view/RtsView.cpp`:
   - Set `packet.animationModelGeneration=ch.modelGeneration` when submitting eligible compiled graph sources to the genre-neutral View.
   - Reject any model choice with `modelGeneration==0` from this accelerated generic path (keep legacy animation instead).
   - Reject presentation inputs lacking a stable `SceneObject::entity` ID, because an index-derived fallback ID can be reused by a different logical object after a model reimport; keep these on legacy presentation. Existing packed valid ECS entity ID remains the stable source key and embeds entity generation.
2. `52f656642720e726f8e6f4ac163c37ad73d9bb24` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`:
   - Allow compatible opt-in pre-retirement named phase carryover in **two precisely defined source transactions**: (a) a different Model object with a different explicit nonzero publication epoch; or (b) the SAME immutable Model address, SAME nonzero publication epoch but a recompiled/replaced Graph, as triggered by an authored graph-only reload.
   - Still require same stable source ID+logical generation, exact copied node-name/parent hierarchy and healthy captured playback. Reject ambiguous mismatched address/epoch combinations conservatively, including allocator-reused address combined with a new epoch and Model-pointer change without epoch bump.
   - The snapshot was captured before detach, while old CPU graph was readable, so no old graph is consulted during the later compatibility check. `AnimationGraphInstance::restorePlayback` still decides named state/clip compatibility and restarts invalid combinations. No old native pose/solver/event state is restored.

## Architecture, compatibility and safety

`ModelChoice::modelGeneration` is a per-published-asset identity, not a gameplay generation. The View-facing `Object::generation` remains a type/choice appearance identity while the `Object::id` is the actual packed valid ECS entity; source reuse across entity respawns gets a different packed ID. Types that cannot prove a stable entity ID or publication epoch are held on the original RTS animation adapter instead of entering a source bridge with ambiguous lifetime.

The zero epoch fallback for OTHER hosts remains source-compatible but cannot safely do cross-detach playback restoration. This change does not alter gameplay ECS data, network/save serialization, animation graph schema, runtime worker ownership, GPU texture ownership, model retirement tokens, graph-notify/montage state, or any shared source aggregate field order. The generic library contains no RTS/FPS names or dependencies.

**Host contract:** the same Model object must not be mutated in place without incrementing its publication epoch. Same-Model same-epoch eligibility applies only to replacement of immutable compiled Graph data. Old graph and CPU model must remain readable until `forgetSourcesForReimport` finishes, and the call must run outside worker dispatch. The old Model address retained by the pending handoff is an integer for conservative equality rejection, not an owning or borrowed pointer to dereference.

## Actual verification vs NOT RUN

**ACTUAL:** read connected GitHub sources `RtsView.cpp`, `RtsView.h`, `RtsLook.cpp`, `View.h`, `MotionWeaveSourceBridge.h`; confirmed `RtsLook::loadChoice` sets `out.modelGeneration=view::ModelPublicationEpoch::next()` and the original `RtsView::Object` packet failed to propagate that value. Submitted two exact GitHub source commits using fetched content SHA guards.

**NOT RUN:** native compile/link, CMake, CTest, source-reimport end-to-end running scene, current SHA CI green, Metal/OpenGL/Vulkan, visual parity, ASan/TSan, high-crowd benchmarks or final test runner, following the user's feature-first/test-once instruction. Do not claim successful runtime phase restoration or ready-to-merge status.

## Remaining gaps and Stage 148

1. Audit generic **FPS/editor/native clients** for stable source ID, nonzero Model publication generation, graph/model source handoff and worker synchronization. Wire only proven-safe lifetime paths and leave zero-epoch legacy behavior unchanged where the host cannot guarantee identity.
2. Inspect authored same-Model Graph swap implementation to guarantee old compiled graph remains valid until optional capture; validate actual source graph recompilation order when tests are finally run.
3. Audit other `ModelChoice` publication paths (streaming, staged choices, LOD) for nonzero generation and no silent reused epoch when models change. Where proof is missing, fall back to legacy instead of fabricating IDs.
4. Continue Universal View adoption for full FPS/RTS/editor animation, multi-LOD retargeting, attachments, notices and graph authorship; preserve a single presentation ECS writer.
5. Append independent Stage 148 handoff. Only after complete functional closure execute `bash tools/animation/validate-final.sh` on the actual Mac plus GPU/visual parity/failure injection; keep draft PR #22 until verified. Main and the separate PR base remain diverged in prior branch comparisons; no auto-rebase/merge/force push.
