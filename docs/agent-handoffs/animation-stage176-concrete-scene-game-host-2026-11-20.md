# AIVERSE MotionWeave — Stage 176: concrete native scene host and canonical SceneAsset builder

Date: 2026-11-20
Working branch: feature/universal-ecs-animation-pipeline
Starting HEAD: 6fcf307a1f703e01a69e7652068cd9dc75c86598 (Stage 175)
Draft PR: https://github.com/chromealex/AIVERSE/pull/22
Target: genre-independent native C++ presentation; no RTS/FPS-specific ECS components.

## Objective

Stage 175 implemented a structurally typed template adapter whose native scene headers existed only on sibling branches. Stage 176 brings the actual **canonical scene definitions currently on main** into the isolated MotionWeave branch and introduces a concrete typed native Simulation/SceneLoadCoordinator game host, together with a deterministic SceneAsset-to-visual builder and a deferred native test target. It does not merge unrelated main work, rewrite SceneRuntime authority, or claim an actual game's prefab-resolver can be invented.

## Exact source commits / changed files

| File | Commit | Change |
|---|---|---|
| engine/scene/SceneAsset.h | 1a4133137a14d7c2e26b2fd033e7cc8991aa99bc | Exact copy of canonical main source blob f98f67e32b701175000f2aa985aee1905b3824c4; deterministic SceneAsset and SceneObjectAsset identity |
| engine/scene/SceneRuntime.h | a2ae9d8d666d7bfa86403986cfede83fe63e99c0 | Exact copy of canonical main source blob d21827519967c47763c18cf4ff918ef018ff25e8; authoritative SceneState / SceneOwner / SceneId / ScenePhase |
| engine/scene/SceneLoadCoordinator.h | 277df536820436eb82012c7a1c3871e1502ab99b | Exact copy of canonical main source blob 423344ad029f4a519396ab2a9caa9cff5f6e5ee0; actual request/ready/prepared/commit/cancel/unload/rollback APIs |
| engine/view/MotionWeaveSceneOwnedHost.h | 3a4428d219026ed4e94b9e5b680a03f9d47b5f8e | Adds short-lived liveResources(sceneId), exposing current scene's REAL ModelResourceCache only until next visual mutation |
| engine/view/MotionWeaveSceneGameHost.h | 9caa2300678f171f4e4429b5b88110739a5f22d7 | NEW concrete class binding Simulation, real SceneLoadCoordinator, Device, RenderDeviceLifetime, and MotionWeaveSceneOwnedHost |
| engine/view/MotionWeaveSceneAssetBuilder.h | 90862aff9a8fee19e339f00f8c626edcbf8e0870 | NEW schema-aware builder: real SceneObjectAsset stable ID, visual resolver, dedicated ModelAssetProvider per scene, all-or-nothing output, source Model resident checks, visual-less prefab skip |
| engine/view/MotionWeaveSceneCoordinatorAdapter.h | 16a931661655def3ee1cf75c7fc0df79e6e32f60 | If native commit fails, discard staged GPU provider then cancel still-Loading/Failed authoritative ticket at structural barrier; preserve original error |
| tests/animation/scene_game_host_api.cpp | 722706d6b0f3c7fff957891ae74f0ff06cbd0077, fe002aa850c6d8874c4dcda27fc4d47a6a03efe6, 36c4f15b8d9d6d5aaa039ad37efc10d318c51172 | NEW compile/API fixture instantiating real coordinator-dependent template, canonical active SceneState filtering, builder type traits, Release-safe error |
| CMakeLists.txt | 821e606f396f8b353e1595226e170d0a9272d95c | Registers deferred desktop CTest animation_scene_game_host_api with native model/engine/thread libraries |
| docs/motionweave-scene-game-host.md | ab672fbc545f624b8462ad6ff0649ad99b57df67 | Native project integration guide and exact limitations |

The three canonical scene files were synced from main as exact copies, not rewritten or copied from stale assistant assumptions. Other main branch files were intentionally not merged. Their includes engine/sim/Simulation.h, engine/sim/SceneHierarchy.h, core Archive and engine/data/Json.h already existed on the MotionWeave branch at source-audit time.

## Public API / usage

**MotionWeaveSceneGameHost** constructor requires an externally owned Simulation&, scene::SceneLoadCoordinator&, gfx::Device&, and RenderDeviceLifetime::Lease on the native owner thread. Provides request(asset,mode,persistent), poll(), ready(ticket), commitReady(ticket,Builder,Stats,error), cancel(ticket), unload(SceneId), restoreResidencyAfterRollback(), sync(SceneId,dt,Stats,error), prepareParent(), prepareChild(), collectRetired(), completeForShutdown().

The real native coordinator remains responsible for SceneOwner/SceneState and deterministic scene spawn. The GameHost is a **presentation-only** owner. It reads canonical active scene IDs through Simulation::world().resourceConst<scene::SceneState>() after successful authoritative commit (preserves persistent scenes) and does not mutate that World.

**MotionWeaveSceneAssetBuilder** receives the real SceneAsset, creates one dedicated ModelAssetProvider, calls a host-provided resolver for each SceneObjectAsset and produces immutable MotionWeave parent/static and independent child Graph appearance snapshots. The object key is set to std::to_string(SceneObjectAsset::id) and never an ECS Entity index. Source and child Models are checked against that scene provider using ModelReadLease. Nonvisual prefab records may return null optional. On failure, a partially uploaded provider is kept in the outgoing unique_ptr so the coordinator adapter can pass it to SceneModelProviderRetirement (actual GPU token, not appearance shared_ptr).

Example host wiring and stage/commit/unload ordering: docs/motionweave-scene-game-host.md.

## Scene lifecycle invariants

1. The authoritative native World is the only owner of SceneState / SceneOwner. MotionWeave owns cosmetic Graphs and presentation Entities in independent Worlds.
2. A scene is staged by the visual builder in a separate ModelAssetProvider and private presentation generation BEFORE coordinator.commit(ticket). Only an accepted authoritative commit publishes the cosmetic generation and retires the old scene provider with GPU-completion proof.
3. On native commit rejection, cancel the remaining failed/Loading scene ticket, discard staged Graphs and provider and retain the old active scene. Do not roll back a successful authoritative scene if the late visual swap/active-IDs callback fails.
4. On scene unload/Replace, detach old parent and child Graphs before retiring their C++ ModelAssetProvider. SceneResourceAdapter dependency reference counts are distinct from the cosmetic provider's GPU ownership.
5. After Simulation::loadState(), restoreResidencyAfterRollback clears old presentation objects, invalidates native coordinator epochs and queues residency-only tickets without respawning gameplay Entities.
6. CPU ModelReadLease and async loaders must finish before cache retirement. SceneModelProviderRetirement holds real Models, texture caches and rollback pools until complete GPU token or successful full-idle proof; a clean token-complete owner may skip an unnecessary second device-wide idle wait, while nested pending queues still use the conservative fallback.

## Scope exclusions and remaining issues

- This is **real native type integration** in shared engine code, NOT an app-specific call inserted into a concrete RTS/FPS/GameModule. The project must provide a native visual resolver mapping its prefab names/properties to Model and Graph resources. We intentionally do not hardwire gameplay modules into MotionWeave or assume a particular game's scene schema.
- The scene source header sync covers the coordinator/runtime/asset contract only. Other main changes were not merged. Main may evolve, so future merges should review exact SHA differences instead of blindly overwriting files.
- This GameHost uses a dedicated unique ModelAssetProvider per scene. True shared-provider/multi-scene reference-counted GPU residency is NOT IMPLEMENTED; sharing an AppearanceAsset snapshot does not suffice.
- A completed scene GPU token can skip repeated device idle only when nested cache and rollback queues are empty. Full resource teardown and all backend latency still need native RHI measurement; no universal nonblocking guarantee.
- Complex facial Control Rig/IK and automatic morph-preserving geometry LOD generation remain separate partly implemented directions.
- Existing MotionWeave Stage 161–175 code is source-implemented but not native-compiled in this chat; do not infer correctness from code presence alone.

## Actual checks performed vs not performed

**ACTUAL:** GitHub connector read of main SceneAsset.h, SceneRuntime.h, SceneLoadCoordinator.h and their source blob SHA; corresponding exact file creation in feature branch; current Simulation.h, ModelAssetProvider.h, CMakeLists.txt and MotionWeave adapters inspected; SHA-guarded commits and source readback. The compile/API fixture was registered in CMake but not built.

**NOT RUN:** native Mac CMake configure/compile/link, CTest including the new fixture, clang -Werror, genuine SceneLoadCoordinator scene activation or rollback in GameModule, GPU fence fault injection, Metal/Vulkan/GLES pixel checks, ASan/TSan/UBSan, CPU/GPU crowd benchmark or VRAM profiling. Do NOT label the feature tested, production-ready or merged.

## Final native acceptance

- Compile test animation_scene_game_host_api on the actual Mac. Check every concrete header, real CMake/CTest link and native SceneState Active-only filtering.
- Full genuine SceneAsset manifest with visual and nonvisual prefabs; test stable IDs, invalid model/read-lease, failed visual resolver after partial GPU upload and scene commit rejection cleanup.
- Multi-scene additive/Replace preserving persistent scenes, correct Graph timing for independently animated child models, no duplicate pose writer.
- Failed native startup/cancel, invalid epoch, rollback and residency-only rehydration with no repeated authoritative Entity spawns.
- GPU retire token Pending/Complete/Failed/no-token and nested rollback resource queues: verify true owner retention and no duplicate texture/model free.
- Complete Mac CMake/CTest, Metal/Vulkan/GLES, sanitizers, 1k/10k/100k crowd and resource memory/idle timing passes as already requested.

## Progress matrix

| Feature | Source status | Remaining |
|---|---|---|
| Universal ECS AnimationGraph / independent child Graphs | Incremental stages 161–164 | native regression |
| Generic skeleton/semantic morph retarget, FBX/glTF 64-channel and authored morph LOD | Stages 165–172 | real assets, GPU/parity/perf |
| Presentation scene transaction + rollback | Stage 173 | native runtime |
| Real provider GPU owner + completed-token fast cleanup | Stages 174–175 | GPU fault/in-flight tests |
| Canonical SceneAsset/SceneRuntime/SceneLoadCoordinator headers in this branch | **Stage 176 synced from main** | upstream merge reconciliation |
| Concrete Simulation+SceneLoadCoordinator GameHost | **Stage 176 source implemented** | native compile, project-specific visual resolver and bootstrap |
| Canonical SceneAsset-to-visual Model builder | **Stage 176 source implemented** | real asset resolver, integration tests |
| Deferred actual native API/CTest fixture | **Stage 176 created** | final Mac compile/run |
| Real shared ModelAssetProvider residency across scenes | NOT IMPLEMENTED | reference-counted native GPU owner |
| Full Control Rig/advanced facial IK/auto morph LOD | PARTIAL | authoring / deformable simplification |
| CMake/CTest/Metal/Vulkan/GLES/sanitizers/perf | NOT RUN | coordinated final Mac acceptance |

## Next Stage 177

Implement a reference-counted shared **real ModelAssetProvider** residency owner across multiple visual scenes without turning appearance shared_ptr into a fake GPU fence. Audit ModelResourceCache and texture-cache shared leases carefully; on final scene release queue the actual provider behind GPU completion. Next, wire a concrete project-specific GameModule prefab resolver/host bootstrap where appropriate, then close remaining advanced facial/IK/LOD functionality. Continue independent handoff per stage. Do not change PR #22 from DRAFT until the final native Mac suite has passed and known unresolved features are closed.
