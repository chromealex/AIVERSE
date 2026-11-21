# AIVERSE MotionWeave — Stage 179: real native SceneAsset / ModelView preview integration

**Date:** 2026-11-20
**Working branch:** `feature/universal-ecs-animation-pipeline`
**Starting HEAD:** `96a250b4da22d17ae140e29e997bc1244da52014` (Stage 178)
**PR:** https://github.com/chromealex/AIVERSE/pull/22 — leave DRAFT, unmerged.
**Policy:** genre-neutral engine; canonical Simulation/SceneOwner/SceneState only; parent animation has one writer; no speculative GPU completion.

## Scope / exact source changes

| Commit | File(s) | Actual authored implementation |
|---|---|---|
| `bd97a893`, `a9446777`, `d1cc2ee2` | `apps/modelview/NativeScenePreview.h` | New opt-in host interface/owned native Simulation, canonical scene coordinator, ModelView per-object animation and frame-scoped packet buffers |
| `790864c7` | `apps/modelview/NativeScenePreview.cpp` | Genuine native GameModule::setup + `scene::registerScenePersistence`, canonical prefab spawn, strict project visual catalogue, real imported GLB ModelAssetProvider/AppearanceRegistry, read-only shared resident resolver, owner-thread scene/rollback lifecycle and per-object native Animator |
| `786fd2c1`, `df60c11f`, `e348367b` | `NativeScenePreview.cpp` | Poll native GPU retirement at owner barrier, discard old epochs' tickets on rollback, rebuild actual SceneHierarchy after loadState, clear stale appearance publications, preflight requested named model clips, invalidate stale Model pointer publication generations |
| `c1cb2e6e` | `apps/modelview/main.cpp` | `--scene-preview <content-root>`, optional `--scene-start <scene-key>`; preserve existing paths; in-scene GPU owner-frame poll, actual View draw, packet lifetime through Renderer3D::render; HUD Add/Replace/Snapshot/Restore; explicit shutdown quarantine |
| `6baa4547` / `2d1de07b` | `CMakeLists.txt` | Add new ModelView implementation TU, register `animation_scene_preview_assets` deferred CTest target |
| `b0308aa4`, `e577aca4` | `templates/showcase/motionweave-visuals.json` | Real showcase slimy GLB and `Wobble` authored native clip, sealed immutable GPU pack, explicit nonvisual marker |
| `943bd36c`, `96d15655`, `e5804509` | `templates/showcase/scenes/motionweave-{global,area,unknown}.json` | Canonical SceneAsset v1 persistent/additive/negative manifest fixtures |
| `3b8261e8` | `tests/animation/scene_preview_assets.cpp` | Deferred Release-active assertions for real manifest IDs, visual/nonvisual catalog registration and actual GLB magic bytes |
| `47ca5ec1` | `docs/motionweave-native-scene-preview.md` | Public instructions, CLI, lifecycle, ownership barriers, known exclusions and pending tests |

The implementation is deliberately in `apps/modelview` rather than
`engine/view` because the project chooses the concrete prefab scheme
and model assets. The generic API still lives in Stage 178's
`engine/view/MotionWeaveScenePrefabBindings.h`. Neither the generic
engine API nor any RTS/FPS authoritative GameModule is modified.

### Actual runtime architecture

`NativeScenePreview::init` creates `Simulation` using a minimal
`GameModule` whose `setup` registers `scene::SceneOwner/SceneState`
with persistent ECS. Its separate `scene::SceneLoadCoordinator`
prepares real on-disk SceneAsset v1 JSON via an immutable capture on an
async CPU worker. The **true canonical prefab spawner** (coordinator
`SpawnPrefab`) creates authoritative ECS Entities with
`LocalTransform` whether or not the prefab has a Model. The
`SceneHierarchy` attaches them at the structural barrier. The
independent visual resolver imports `models/slime.glb` via the real
`ModelAssetProvider`, publishes immutable appearance snapshots, and
retains the whole GPU Model owner through Stage 177 shared residency
leases and RHI completion-token retirement.

`NativeScenePreview::pollAndCommit` only accepts Ready tickets BEFORE
a render pass. `commitReadyShared` stages the cosmetic generation,
calls actual authoritative `SceneLoadCoordinator::commit`, and then
reconciles real Active SceneState IDs (persistent scenes retained after
Replace). A repeated shared scene gets a resident-only callback and
does not import/mutate the occupied GPU provider. Unknown prefab,
missing GLB, mismatched read lease or nonexistent named clip fail
before the canonical commit.

`NativeScenePreview::submit` advances ONE native `Animator` per
visual `(SceneId,SceneObjectId)` key, evaluates the parent pose once,
prepares ModelReadLease-protected draw packets via
`MotionWeaveSceneGameHost::prepareParent` and submits to the generic
`View`. `finishRender` destroys Prepared packets only AFTER
synchronous Renderer3D::render. `collectRetired` runs at the next
owner frame barrier; GPU provider release remains RHI-token guarded.
No generic Graph is evaluated a second time.

`Snapshot` saves actual `Simulation`. `Restore` runs
`Simulation::loadState`, `SceneHierarchy::rebuild/update`,
drops old cosmetic snapshot/Animators/appearance cache and calls
`GameHost::restoreResidencyAfterRollback`. Restore tickets are
residency-only so authoritative prefab Entities are NOT respawned.
Missing GPU shutdown proof is diagnosed and unresolved GPU owners
quarantined, rather than double-freeing a resource still in flight.

Public demo in
`docs/motionweave-native-scene-preview.md`; initial mode:
`./build/ecs-test/aiverse_modelview --scene-preview templates/showcase`.

## Validation performed (actual vs not)

**ACTUAL:**
- Reviewed GitHub-connected native APIs for Simulation::setup and
  GameModule, SceneAsset, SceneLoadCoordinator, SceneRuntime,
  SceneHierarchy::rebuild, ModelAssetProvider, ModelResourceCache,
  AppearanceAssetRegistry, RenderDeviceLifetime,
  SceneGameHost and ModelView's existing Renderer3D/Animator paths.
- Verified real showcase directory includes `models/slime.glb`,
  referenced by `templates/showcase/rules.json` with authored
  `Wobble` clip; source-level consistency and callbacks audited.
- GitHub connector committed code/content/tests/CMake/docs with
  blob-SHA-checked replacement of existing files; later readback
  showed updated paths and no visible conflict markers.

**NOT RUN:** CMake configure, native compile/link of new ModelView TU,
CMake/CTest `animation_scene_game_host_api` or
`animation_scene_preview_assets`, actual graphics startup, GLB clip
import/play, snapshots/Replace live, Metal/Vulkan/GLES parity,
ASan/TSan/UBSan, GPU-fence failure injection or VRAM/crowd performance.
Do NOT call the demo validated/proven. No local AIVERSE native worktree
is available in this tool environment; GitHub file operations are not
a substitute for successful compilation.

## Status matrix

| Feature | Status after Stage 179 | Remaining |
|---|---|---|
| Generic AnimationGraph/MotionWeave/ECS | Prior stages source implemented | Final native suite |
| Real scene GameHost and shared GPU Model owner | Stage 176–177 source implemented | RHI lifecycle tests |
| Sealed project prefab visual registry | Stage 178 source implemented | Production consumer QA |
| **Actual native standalone GameModule/SceneCoordinator/SceneAsset bootstrap** | **Stage 179 SOURCE IMPLEMENTED** | Native compile and live real-scene test |
| **Additive/persistent Replace and rollback residency-only hook** | **Stage 179 SOURCE IMPLEMENTED** | Snapshot and lifetime acceptance |
| **Real GLB native clip owner + View submission** | **Stage 179 SOURCE IMPLEMENTED** | ModelView visual/pose acceptance |
| Facial Control Rig / full IK authoring | PARTIAL | Advanced authoring + runtime |
| Automatic morph-preserving LOD simplification | NOT IMPLEMENTED | New geometry algorithm |
| Full Mac CMake/CTest/RHI/sanitizers/perf | **NOT RUN** | One-pass final campaign |

## Exact next Stage 180

Finish the remaining **generic** advanced facial Control Rig / IK
authoring and runtime control layers with compiled semantic morph
controls. Reuse MotionWeave generic retargeting and sparse 64-channel
morph pipelines, avoid reintroducing animation implementation inside
any FPS/RTS engine core. Keep the test campaign deferred until
functionality closes, then run `aiverse_modelview`,
`animation_scene_game_host_api`,
`animation_scene_preview_assets` and complete applicable animation,
render, SceneLoadCoordinator, Metal/Vulkan/GLES, sanitizers and crowd
suites on the user's Mac. Fix compiler/link/runtime failures found
there before releasing PR #22.

No automatic morph-preserving geometry simplifier, authoring UI for
52 ARKit blendshapes, graph-driven parent preview or live project
integration beyond the opt-in ModelView scene sample was claimed.
Do not merge PR to main until native acceptance.
