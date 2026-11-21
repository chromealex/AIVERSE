# AIVERSE MotionWeave / Universal View — Stage 138 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 137 feature-freeze policy](animation-stage137-feature-freeze-validation-plan-2026-11-19.md)

## User-approved execution order

The user explicitly requested **complete all remaining features first, then run all tests in a single organized pass**. Consequently this is a feature-only stage. No new tests were written or run; do not start CTest/CMake/ASan/TSan/benchmarks between unfinished feature stages. Continue separate per-stage source/handoff commits. The one-command runner `tools/animation/validate-final.sh` is prepared for a future macOS checkout AFTER genuine feature freeze.

## Branch and integration safety

- Continued from Stage 137 HEAD `b03931efed11d3e38c104e648aa7bb5659d7cf56`.
- Latest code HEAD before documentation: `b2f5ac478b276d8acb17ed8ef2de6a81adeb7d23`.
- At code readback, current `main` and feature were divergent, feature **ahead 1028 / behind 305**. The separate PR base `feature/universal-entity-inspector-fps` remained divergent, feature **ahead 1398 / behind 615**.
- `main` also changes Model/renderer/graphics/batching files. Changes below are on this isolated animation branch only; **no main/peer modifications, rebase, force push, merge, retarget or cherry-pick**. Compare Model.cpp with peer changes before integrating.
- PR #22 stays Draft; no build/test pass is implied.

## Implemented code with exact commits

1. `8ad42fcccac0ea7cb63872c5b01d79260143e839` — `engine/render/Model.cpp`:
   - `uploadModel` now rejects a dimensioned image lacking a valid `gfx::TextureHandle` after either **shared texture cache** or **speculative dedicated texture** upload. Previously only speculative (`publishSharedTextures=false`) images were checked, so regular Model publication could succeed with a missing GPU image.
   - When morph deltas need an RGBA32F texture, a failed `dev.createTexture` now makes `uploadModel` return false. CPU `Primitive::morphDeltas` remain intact on failure; morph CPU deltas are cleared **only after** a valid `m.morphTex` exists.
   - `ModelTextureCache::upload` now preallocates the shared lease and GPU owner `std::map` node **before** calling `uploadImage`. This avoids CPU allocation failures *after* GPU texture creation that could have abandoned a live handle in a local Image. If the GPU upload creates no texture, the empty map node is removed and the original Image remains unpublished.
2. `46a050552f0a49f604c6745976c0c09fcf5de4f4` — `modules/rts/view/RtsLook.cpp`:
   - Synchronous RTS Model loading now checks `uploadModel` result and rejects a partial GPU upload.
   - Any exception after a Model has been constructed (including a partially created GPU geometry/image/morph allocation) transfers the scratch Model to `resources_.retireUnpublished(dev,m)` rather than calling `m.reset()` while the GPU may still reference its handles.
   - Retains the legacy null-Model failed-load tombstone path for safe RTS fallback, without publishing broken geometry.
   - The existing asynchronous RTS `prepareType` path from Stage 136 already checks `publishPrepared` failure and advances its source queue.
3. `a6f45ef32c4b7f7f9c191b6061ad88116ce6678f` — `engine/render/Model.cpp`:
   - `addImage` reserves Model image vector capacity before GPU creation, and returns `-1` when no valid texture was uploaded rather than inserting an invalid material slot. This matches existing callers' `-1` fallback convention.
4. `7807aa041124660b4c248a05ec18f73b34b319ff` — `engine/render/Model.cpp`:
   - Replaces the potentially throwing `shrink_to_fit` after a new material image GPU upload with an empty-vector `swap` that releases CPU pixel storage without allocation.
5. `b2f5ac478b276d8acb17ed8ef2de6a81adeb7d23` — `engine/render/Model.cpp`:
   - Replaces post-GPU-upload `shrink_to_fit` operations for source image pixels, morph deltas and main vertex/index buffers with allocation-free empty-vector swaps. After a successful texture/buffer upload, CPU buffer cleanup should no longer throw while retaining active GPU handles.

No public MotionWeave graph signature, ECS authority, CPU worker policy, source publication epoch, gameplay/network/save schema or GPU fence policy changed.

## Ownership and failure invariants

- An invalid image/morph handle leads to `uploadModel=false`; `ModelResourceCache::publishPrepared` (Stage 136) then transfers the partially uploaded Model to the **existing GPU-fenced retirement queue**.
- A thrown GPU allocation from the synchronous RTS loader is also handled by owner-controlled `retireUnpublished`, not by immediate `unique_ptr<Model>` destruction. The scratch GPU resources are released only after actual completion, or held/orphaned conservatively if a fence cannot prove completion.
- A shared texture that was successfully published by the texture cache before a later Model failure remains owned by the shared texture cache. The failed Model drops its shared borrow on GPU-safe release; the texture cache's normal lease-aware trim/shutdown owns eventual reclamation.
- `addImage` reports failure as `-1`, not a misleading valid index. Hosts that never check this sentinel retain their own fallback responsibilities.
- The shared texture owner map node exists temporarily with an invalid texture *on the render-owner thread only* while `uploadImage` runs. This is **not** a concurrent cache or permission for reentrant GPU upload from callbacks.
- No new GPU backend call, allocator, fence operation, device epoch or RHI API was added.

## Actual validation vs unexecuted work

**ACTUALLY PERFORMED:** GitHub connected source inspection of `engine/render/Model.cpp`, `Model.h`, `ModelTextureCache.h`, generic cache, RTS sync/async host and Stage 137 handoff; exact blob-SHA code commits and source readback verifying image/morph failure checks, shared-owner preallocation, nonthrowing buffer release, and RTS safe retirement; GitHub current branch/main/PR-base comparisons.

**NOT RUN BY USER-APPROVED FEATURE-FIRST POLICY:** local AIVERSE CMake configure/C++ compiler/link, any CTest, macOS Metal, Linux/Windows GL/Vulkan, GPU failure injection, sanitizer, model hot reload screenshots, FPS/RTS/editor visual parity, benchmarks and CI validation for the new commits. All prior Stage 132–136 tests likewise remain **authored but not verified passing**. Do not assert release/merge readiness or measured correctness.

## Remaining feature work before freeze (Stage 139 onward)

- **Direct native Model upload callers** outside generic `ModelResourceCache::publishPrepared` and RTS sync path: audit remaining `uploadModel` calls in road/shore/preview/editor/modelview/geometry/runtime modules for ignored boolean results and direct destruction of partially uploaded Models. Coordinate with the owning terrain/geometry/graphics agents before editing their overlapping paths, and use host-owned deferred retirement. Do not force this animation branch over peer work.
- Preserve texture/RHI feature ownership: real image/morph partial-failure injection and visual parity only in the final user-approved test pass, after feature closure.
- Complete outstanding generic View migration / legacy RtsView presentation adapters and native FPS/RTS/editor animation continuity, retargeting and lifetime contracts as documented in Stage 137's feature-closure gate.
- Resolve current main and PR-base divergence only in a separate reviewed integration worktree, **not** by rebasing shared animation branch or unreviewed merge.
- Next Stage 139: choose the next specific unimplemented host feature, make bounded source changes, document exact commits, API, risks and feature-freeze status in a new handoff. **Do not run tests until feature freeze.**
