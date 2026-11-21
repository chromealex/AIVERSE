# AIVERSE MotionWeave — Stage 178: sealed project-supplied prefab visual bootstrap

**Date:** 2026-11-20
**Branch:** `feature/universal-ecs-animation-pipeline`
**Base at start:** `0be2a829ae6b98b1d4cc976f90c69246f2da1be4` (Stage 177 and its size_t follow-up).
**PR:** https://github.com/chromealex/AIVERSE/pull/22 — leave **DRAFT / unmerged**.
**Authority constraints:** shared animation/View may NOT depend on game genres, prefab gameplay component types, simulation steps, SceneOwner mutation or Box3D; no second Graph pose writer.

## Feature implementation (source authored)

| Commit | File | Exact change |
|---|---|---|
| `323a12e723cf6ac70d6e7da944116f7e88cb9700` | `engine/view/MotionWeaveScenePrefabBindings.h` | New reusable native project prefab-to-visual registry, typed dedicated/shared SceneGameHost commit methods |
| `f0ca434c67b17df2e89fbabb236f1f135b0ab4ac` | `tests/animation/scene_game_host_api.cpp` | Native type-instantiated shared prefab bootstrap and Release-active registry duplicate/seal guard checks |
| `12947e2c2f4bbddca858e74f08bb956f799cf836` | `engine/view/MotionWeaveScenePrefabBindings.h` | Reject/cancel Ready ticket for unsealed binding without allocating a fake GPU owner or publishing an arbitrary key |
| `4e9a216a653776c810b25f597cd35ea20245fa49` | `docs/motionweave-scene-prefab-bootstrap.md` | Public owner-thread integration guide and project callback signatures |

### API and lifecycle

A project registers each `SceneObjectAsset::prefab` as either
`registerVisual(key,freshResolver,residentResolver)` or
`registerNonvisual(key)`. Both callbacks must be supplied for visual
prefabs; the resident one sees only `const ModelAssetProvider&`. Unknown
prefab keys reject the scene rather than creating invisible missing
models. `seal(immutablePackKey)` freezes the registry and pins a
versioned, host/device-epoch-scoped residency key, so later additive
scenes cannot quietly change the binding policy while sharing a real
GPU Model provider.

`commitReady(host,ticket,device,lease,reader,stats,error)` drives the
existing `MotionWeaveSceneAssetBuilder` with dedicated provider lifetime.
`commitReadyShared(...)` creates the FIRST complete actual provider
then resolves all later scenes through the read-only
`MotionWeaveSceneAssetBuilder::buildResident`. Both call the genuine
`MotionWeaveSceneGameHost` and `SceneLoadCoordinator` at the
native structural barrier, which alone commits ECS scene membership.
The authoritative spawn remains supplied to the separate native
`SceneLoadCoordinator::SpawnPrefab`. Cosmetic object IDs are the
stable decimal `SceneObjectAsset::id`; never `Entity::index`.

Registering a nonvisual marker does not prevent the authoritative
SceneLoadCoordinator from spawning that Entity. New visuals must
satisfy the existing native ModelReadLease and Model/Graph publication
checks; failed builders/cancel retain old Active scenes and retire
partial native GPU providers. Shared providers are immutable; the
second scene cannot upload a missing Model into a live resident pack.
Replace uses canonical Active SceneState IDs (persistent scenes survive);
rollback uses existing residency-only requeue without gameplay respawn.
No gameplay-/genre-specific types have entered the View/animation core.

## Review and tests — distinguish actual from pending

**ACTUALLY PERFORMED:** GitHub connector read of the Stage 177 handoff,
canonical `SceneAsset`, `SceneLoadCoordinator`,
`MotionWeaveSceneGameHost`, `MotionWeaveSceneAssetBuilder`,
`MotionWeaveSceneOwnedHost`, ScenePublication source; exact source
commits through GitHub's authenticated contents API with checked source
blob SHA on updates. Repository could not be cloned from the local
working container (no DNS/network access to github.com).

**NOT RUN:** CMake configure, C++ compiler, link, CTest including the
modified `animation_scene_game_host_api`, real prefab/Model imports,
native GameModule scene activation or rollback, Metal/Vulkan/GLES shader
compilation or pixels, GPU fence failure injection, ASan/TSan/UBSan,
1k/10k/100k animated crowd, idle/VRAM measurements.
**No PASS claim for code/fixture.** Typed fixture is committed source,
not a successfully executed test.

## Readiness

| Area | Status | Next required step |
|---|---|---|
| AnimationGraph, Montage, IK/retarget core, independent child Graph, 64 morphs | Previous stages SOURCE IMPLEMENTED | Mac compiler / real asset tests |
| Concrete SceneAsset and SceneLoadCoordinator/GameHost integration | Stage 176 SOURCE IMPLEMENTED | Genuine GameModule runtime |
| Shared actual native Model GPU owner/leases across scenes | Stage 177 SOURCE IMPLEMENTED | Native fault/load/VRAM tests |
| Project-installable prefab mapping, nonvisual/unknown policy, sealed shared pack ID | **Stage 178 SOURCE IMPLEMENTED** | Native compiler and real project resolver registration |
| Dedicated/shared concrete GameHost prefab commit dispatch | **Stage 178 SOURCE IMPLEMENTED** | Live additive/Replace/rollback acceptance |
| Actual installed concrete app/genre GameModule prefab/Model mappings | **NOT IMPLEMENTED** | Choose real assets + bootstrap owned by that app; core remains genre-neutral |
| Automatic shared pack growth/immutable asset fingerprint | NOT IMPLEMENTED | Generation migration/policy if needed |
| Complete facial Control Rig, advanced IK authoring, automatic morph-preserving mesh simplifier | PARTIAL / NOT FINISHED | Separate stages |
| CMake/CTest, Metal/Vulkan/GLES/sanitizers, crowd/perf | **NOT RUN** | One coordinated macOS acceptance after functional closure |

## Explicit next Stage 179

Inspect actual `apps/client/GameClient`, native `GameModule` start-up
and project prefab/Model authoring schemas on the current main branch
without overwriting concurrent peers. Connect a **real project-owned**
`SceneLoadCoordinator::SpawnPrefab` and the new visual registry to
one application SceneAsset with valid Model/Graph asset readers. If no
canonical application scene manifest/visual registration exists,
first add an opt-in sample native bootstrap (with a valid test asset)
rather than fabricating a game-specific prefab contract inside View.
Verify at owner structural barrier that visual-less markers still spawn
authoritatively; additive persistent scenes survive Replace; rollback
rehydrates only visuals; unknown/missing GPU models reject safely.
Then finish advanced facial Control Rig/IK and mesh morph-LOD
simplification. Full native Mac tests remain a **single coordinated
acceptance campaign after functionality closes**, per user request.

Deferred first command on the user's Mac:

```sh
cmake --build build/ecs-test --target aiverse_animation_scene_game_host_api_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^animation_scene_game_host_api$'
```

If the branch advances concurrently, inspect fresh HEAD, keep peer
changes, and append to `docs/agent-handoffs/animation-universal.md`;
never force-push or merge to main prematurely.
