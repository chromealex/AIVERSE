# MotionWeave — real SceneAsset / SceneLoadCoordinator preview in ModelView

**Stage 179 · 2026-11-20 · status: source authored, not yet compiled/visually validated**

This is an **opt-in native application path** that uses a real
`Simulation`, `GameModule`, `scene::SceneLoadCoordinator`,
`view::MotionWeaveSceneGameHost`, and Stage 178's sealed
`MotionWeaveScenePrefabBindings`. No FPS/RTS GameModule or View class is
imported into the shared MotionWeave engine. The existing model/gallery/
attachment modes are unchanged by default.

## Try the actual content

```sh
cmake --build build/ecs-test --target aiverse_modelview \
    aiverse_animation_scene_game_host_api_tests \
    aiverse_animation_scene_preview_assets_tests
ctest --test-dir build/ecs-test --output-on-failure \
    -R '^animation_scene_(game_host_api|preview_assets)$'

# From the AIVERSE checkout root, on the owner's graphics device:
./build/ecs-test/aiverse_modelview --scene-preview templates/showcase
```

If your generator places binaries elsewhere, use that output directory.
These commands are **instructions to run on macOS, not claimed results**.

The preview starts `scenes/motionweave-global` (persistent). Its
`visual/slime` prefab imports the **real**
`templates/showcase/models/slime.glb` and starts the authored
`Wobble` native clip via one ModelView-owned `Animator`. Its
`logic/marker` prefab creates an authoritative ECS Entity but
deliberately **no visual Model**.

The preview's HUD shows canonical Active scene counts, authoritative
scene-owned Entity count, submitted visual parent draws and shared
ModelAssetProvider reference count. It exposes:

- **Add scene:** additive `scenes/motionweave-area`; only an immutable
  resident Model is reused, so the second scene owns a new visual
  generation and a second GPU provider lease, **not** a second Model upload.
- **Replace scene:** a native `SceneLoadCoordinator` Replace; the
  persistent global remains Active, nonpersistent prior scenes retire
  through the RHI completion queue.
- **Snapshot / Restore:** actual `Simulation::saveState/loadState`,
  `SceneHierarchy::rebuild`, `GameHost::restoreResidencyAfterRollback`,
  then **residency-only** visual jobs. Gameplay Entities are not spawned
  again during restore.

Controls are host owner-thread structural operations. `SceneLoadCoordinator`
prepares JSON on async CPU workers, but never mutates ECS or GPU on those
workers. `NativeScenePreview::pollAndCommit()` accepts a prepared ticket at
the owner-frame barrier before the next `Renderer3D` submission and
reconciles canonical Active SceneState IDs. Failed prefab imports, unknown
prefabs or invalid named clips abort the candidate without unloading
previously Active scenes.

`NativeScenePreview::submit()` obtains frame-local
`ModelReadLease`-carrying Prepared packets and submits them via the shared
`View`. Only the native ModelView `Animator` evaluates the parent pose
once; the scene host never evaluates it a second time. All Prepared packets
remain alive until synchronous `Renderer3D::render()` completes, then
`finishRender()` releases CPU leases. GPU resources are still retired
later using actual RHI completion proofs. `shutdown()` must finish before
the owner's `gfx::Device` is destroyed.

## Project authoring

Opt-in catalogue `<content-root>/motionweave-visuals.json`:

```json
{
  "version": 1,
  "residencyKey": "showcase/motionweave-slime-pack@v1",
  "prefabs": [
    {"key": "visual/slime", "model": "models/slime.glb", "clip": "Wobble"},
    {"key": "logic/marker", "nonvisual": true}
  ]
}
```

Scene JSON is canonical `SceneAsset` v1 at
`<content-root>/<scene-key>.json`. A scene root `id` MUST equal its
requested scene key. Each prefab has a stable uint64 scene-local `id`,
optional `parent` and integer `properties.x/y/z` for this preview's
authoritative `LocalTransform` setup. The generic engine SceneAsset has
no such integer-only limitation; this is the preview's minimal spawner.
Parenting is handled by the real `SceneHierarchy` in canonical object
order. Only prefab keys registered in the sealed catalogue are allowed.
The included `scenes/motionweave-unknown.json` is a deliberate negative
fixture and must fail if requested.

`NativeScenePreview` is an **application-owned test/demo host**, not
a replacement SceneRuntime or an animation source in gameplay authority.
A real game installs its own GameModule::setup, prefab spawner, render
packet transforms and animation policy using the same public
`MotionWeaveScenePrefabBindings` API. For actual runtime parent
AnimationGraphs and independent animated child attachments, use the
existing MotionWeave SceneGameHost parent/child Graph source API; this
simple preview uses native `Animator` for parent clips and deliberately
rejects `animationGraph` catalogue entries rather than inventing a
second Graph evaluator.

## Boundaries and deferred verification

- The visual catalogue is immutable after `seal()`. The residency key
  must reflect the real model/rig/Graph pack version and GPU epoch. No
  automatic pack fingerprint or shared live provider expansion exists.
- A second scene's resident visual callback **cannot import or mutate**
  the shared native GPU owner. Any missing model rejects; changing
  content requires a new pack generation.
- CPU ModelReadLease is not a GPU fence. Shutdown will quarantine unresolved
  owners if completion is not proven.
- This example verifies regular native bone/morph clip playback subject
  to actual GLB contents. Advanced Control Rig, facial IK, root-motion
  reconciliation, and morph-preserving automatic LOD simplification are
  **not implemented by this demo**.
- No actual configure/build/compiler/CTest, screenshot, real glTF/GLB
  playback, Metal/Vulkan/GLES GPU parity, sanitizer, RHI-fault or
  crowd/VRAM performance run occurred in Stage 179. All implementation
  descriptions are source-level until user runs the final Mac acceptance.

Sources: `apps/modelview/NativeScenePreview.{h,cpp}`,
`apps/modelview/main.cpp`, `templates/showcase/motionweave-visuals.json`,
`templates/showcase/scenes/motionweave-*.json`,
`tests/animation/scene_preview_assets.cpp` and
`engine/view/MotionWeaveScenePrefabBindings.h`.

See [Stage 179 handoff](agent-handoffs/animation-stage179-native-scene-modelview-2026-11-20.md).
