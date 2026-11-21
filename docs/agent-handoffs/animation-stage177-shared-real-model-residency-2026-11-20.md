# AIVERSE MotionWeave — Stage 177: reference-counted REAL scene Model GPU residency

**Date:** 2026-11-20
**Branch:** `feature/universal-ecs-animation-pipeline`
**Starting HEAD:** `efdd439bc471d5f94ed92544bbf9f6b6dee70785` (Stage 176)
**Source HEAD before this handoff:** `12410e373e133805a621469a7743417ef4584f20`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/22 — keep DRAFT and unmerged until final native acceptance.
**Hard requirements:** genre-neutral presentation ECS only; no animation authority over game logic, lockstep, SceneState, SceneOwner or Box3D. One standalone handoff per stage. Feature code first, one coordinated Mac CMake/CTest/RHI acceptance pass later.

## Objective and implementation

Stage 176 had a concrete `Simulation + SceneLoadCoordinator` native MotionWeave GameHost and a safe dedicated `ModelAssetProvider` per scene, but a second additive scene always uploaded another provider. Stage 177 adds **opt-in** multi-scene sharing of the **whole actual Model GPU owner**. It does not confuse shared immutable AppearanceAsset snapshots with ownership of Models, textures, image rollback handles or in-flight GPU submissions.

| Commit | Path | Actual change |
|---|---|---|
| `dd411c60` | `engine/render/SceneModelProviderResidency.h` | NEW keyed native ModelAssetProvider registry with reference counts, per-entry monotonic generation and final-release handoff into existing SceneModelProviderRetirement |
| `9e1569d4` / `a74c4574` | `SceneModelProviderResidency.h` | Leases are **noncopyable** and **move-only**, moved-from token explicitly invalidated; release is idempotent |
| `4d32f0c8` / `d5c9ba9b` | `engine/view/MotionWeaveSceneOwnedHost.h` | NEW `stageShared`, shared live/staged lease slots and release paths on stage/commit/discard/unload/Replace/rollback; stage exceptions clean superseded staged leases; unchanged legacy per-scene provider API |
| `74d56cbb` | `engine/view/MotionWeaveSceneCoordinatorAdapter.h` | NEW `commitReadyShared`: native prepared/commit/cancel with shared-mode builder, rollback/error-safe candidate and actual canonical ACTIVE scenes |
| `74cbf4b8` | `engine/view/MotionWeaveSceneGameHost.h` | Exposes `commitReadyShared`, read-only `sharedProvider` and `sharedProviderReferences` in concrete native Simulation/SceneLoadCoordinator host |
| `d3722426` | `engine/view/MotionWeaveSceneAssetBuilder.h` | NEW `buildResident` read-only prefab resolver; stable SceneObjectAsset ID/duplicate checks and actual ModelReadLease validation |
| `981a082c` | `tests/animation/scene_game_host_api.cpp` | Deferred compiler instantiation of real shared SceneLoadCoordinator commit and move-only lease type assertions |
| `0ef27a5c` | `docs/motionweave-shared-scene-gpu-residency.md` | NEW integration/API design, immutable shared source policy, example, limitations and pending tests |
| `12410e37` | `docs/motionweave-scene-game-host.md` | Guide updated for shared residency; legacy dedicated option retained |

No `main` modification or full merge was attempted. Branch may receive parallel agents' commits; **inspect HEAD/merge base before rebasing/cherry-picking**.

## Public API

```cpp
// Native GameHost owns the same gfx::Device and Simulation as Stage 176.
bool commitReadyShared(SceneTicket ticket, const std::string& residencyKey,
                       Build&& build, Statistics& stats,
                       std::string* error = nullptr);

// Build(const SceneAsset& manifest,
//       const render::ModelAssetProvider* residentOrNull,
//       MotionWeaveSceneGameHost::Owner& newProvider,
//       std::vector<MotionWeaveSceneGameHost::Object>& objects,
//       std::string& error) -> bool
```

When `residentOrNull==nullptr`, builder creates a new *complete* provider by the already existing `MotionWeaveSceneAssetBuilder`. When it is nonnull, builder must leave newProvider null and produce snapshot objects solely from already resident Model/Graph resources through `MotionWeaveSceneAssetBuilder::buildResident`. The builder's `ResolveResident` receives const ModelAssetProvider; no new import, cache invalidate, device upload or mutation is permitted for shared refs.

The first scene owns **one real provider** with one live lease. Each subsequent staged or committed scene owns an independent lease, not a new GPU owner. New staged lease acquisition occurs before retiring the old staged lease for same-key replacement, avoiding a refcount-zero gap. Native scene commit remains authoritative; only after success does the cosmetic generation swap, which then releases the old live lease. Last lease release puts the *unique* provider into `SceneModelProviderRetirement`, which waits for ordered GPU proof or valid fallback (including nested model/image retirement). Failed builders/commit/cancel discard **only** their candidate. `unloadAbsent` takes complete canonical SceneState Active IDs so still-active persistent scenes stay pinned. After `Simulation::loadState`, rollback clears visual worlds/leasing and coordinator requeues residency-only jobs.

## Safety constraints and known limitations

- GPU completion is proven by the existing retirement queue, **not** by CPU refcount zero or ModelReadLease. The host must stop recording new frames and release all frame-scoped Prepared/ModelReadLease data before a structural release.
- A provider key is versioned **within one host and RHI device epoch** and must identify the exact compatible, immutable model/rig/Graph resource pack. Two incompatible packs must NEVER share a key. Residency cannot be acquired cross-host/cross-device. No automatic key fingerprint/asset dependency resolver is implemented.
- Sharing does not automatically prewarm every future scene; the project must prewarm all desired assets in the first provider or reject subsequent missing assets. Mutating a live shared cache, automatic cross-scene Model source imports and online shared provider expansion are **NOT** implemented.
- Intentional GPU owner quarantine on missing safe shutdown remains a diagnosed failure (not a successful nonblocking teardown). Provider refcount is not a GPU fence.
- Some finished scene providers still need full idle fallback for nested model/texture/image retirement; a fully nonblocking backend teardown is not claimed.
- No concrete RTS/FPS or custom GameModule prefab-to-visual bootstrap was changed, by design. Genre-independent engine API is present; application-specific host setup still requires integration.
- Stage 161–176 major facial/retarget/Graph/Model work remains **uncompiled** in this environment. Advanced facial Control Rig + IK authoring and automatically generated morph-preserving mesh simplification remain separate open features.

## Checks performed

**ACTUAL:** connected GitHub read of the Stage 176 scene host, ModelAssetProvider, ModelResourceCache::acquireModelRead const API, GPU retirement queue, canonical SceneLoadCoordinator; SHA-guarded changes via GitHub; readback of modified shared source files and absence of merge-conflict markers; deferred typed native C++ fixture source changed. No direct filesystem clone was available.

**NOT RUN:** CMake configure, compiler, link, CTest (including `animation_scene_game_host_api`), full runtime SceneAsset load/rollback, SceneLoadCoordinator prefab integration, shader compilation, Metal/Vulkan/GLES parity, ASan/TSan/UBSan, GPU completion-token failure injection, 1k/10k/100k crowd, VRAM/idle benchmark. **NO PASS claim.** Current source may still contain compiler/runtime bugs until native validation.

## Stage readiness matrix

| Capability | Status | Remaining work |
|---|---|---|
| Universal AnimationGraph / presentation ECS / one pose owner | Earlier stages source-authored | Native acceptance |
| Independent child Graphs/hot reload/retarget/morph 64 | Earlier stages source-authored | Real asset, GPU and perf validation |
| Transactional scene stage/commit/rollback | Stage 173–176 source-authored | Real GameModule end-to-end acceptance |
| Dedicated provider lifetime and ordered GPU retirement | Stages 174–176 source-authored | GPU fault/stall tests |
| **Shared actual GPU provider across additive scenes** | **Stage 177 SOURCE IMPLEMENTED** | Native compile/runtime proof |
| **Shared stage/commit/Replace/cancel/rollback refcount release** | **Stage 177 SOURCE IMPLEMENTED** | Fault injection and transition tests |
| **Native GameHost shared adapter + read-only resident visual builder** | **Stage 177 SOURCE IMPLEMENTED** | Concrete prefab policy and asset fixtures |
| Auto import/expand immutable shared resource packs | NOT IMPLEMENTED | Carefully coordinated provider-generation migration |
| Full facial Control Rig / advanced IK / automatic morph LOD simplifier | PARTIAL | Artist workflow and model algorithms |
| Metal/Vulkan/GLES / CMake/CTest / sanitizers / perf | NOT RUN | One coordinated Mac acceptance after feature closure |

## Next Stage 178

Inspect actual native app/GameModule bootstrap and integrate a **genre-neutral, project-supplied SceneAsset prefab-to-MotionWeave visual resolver** at a real deterministic structural barrier. If GameModule lives on another branch, review/sync its canonical APIs narrowly rather than replacing its architecture. Handle visual-less prefabs, additive persistent Replace and residency-only restore. Do not hardwire `RTS`/`FPS` labels into shared engine classes. Then finish remaining facial Control Rig/advanced IK/automatic morph LOD functional gaps and perform the requested **one-pass Mac native acceptance campaign**. Preserve PR #22 as Draft until verified.

Never merge to main solely from source inspection. On the user's Mac:
```sh
cmake --build build/ecs-test --target aiverse_animation_scene_game_host_api_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^animation_scene_game_host_api$'
```
Run the full relevant motionweave + renderer + scene suite only after functional feature closure, including Metal, Vulkan and GLES builds/checks wherever supported.
