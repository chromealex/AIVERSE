# AIVERSE — genre-neutral SceneAttachmentHost

Stage 160 adds **stable scene + prefab object identity** and a scene lifecycle
host for the existing `EcsAttachmentScene` **presentation World**. None of
these types are an RTS/FPS/gameplay mechanic.

## Generic published attachment layout

The optional child `id` is a stable **authored key within one parent
prefab**, independent of its current ECS Entity index or Model pointer:

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {
      "id": "left-hand-tool",
      "model": "models/torch.glb",
      "socket": "Hand.L",
      "enabled": true,
      "local": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0.02,0,0,1],
      "childWorld": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    }
  ]
}
```

`AttachmentLayoutCodec` permits legacy files with no `id`. Such entries
are indexed by their position and cannot be resolved by a stable key.
Explicit nonempty IDs must be **unique within the document** and no more
than 128 characters. Malformed/duplicate IDs are rejected before changing
the last-good parsed document or publishing an ECS scene.

The `SceneAttachmentChildComponent` stores this authored ID alongside
parent Entity generation, source Model name, named socket, visibility and
full affine transforms. A published layout still creates one ECS child
Entity per authored item (including disabled items); only enabled entries
are imported into the native render packet.

**Important:** an authored key remains stable across document reloads;
the associated **ECS Entity handle does not**. All scene replacement and
reimport paths intentionally produce new generation-safe handles.

## Native SceneAttachmentHost API

```cpp
#include "engine/view/SceneAttachmentHost.h"

aiverse::World visualWorld; // cosmetic presentation World, not Simulation
aiverse::view::SceneAttachmentHost host(visualWorld);

auto visualParent = visualWorld.create();
uint64_t nativeSceneId = 17;   // allocated by the host scene manager
std::string objectKey = "42";  // stable, scene-local object ID
std::string error;

aiverse::Json layoutJson = /* validated attachment layout */;
bool published = host.activate(
    {nativeSceneId, objectKey},
    visualParent,
    layoutJson,
    publishedSourceAppearance,
    modelProvider.resources(),
    [&](const std::string& path) {
        return appearances.find(path);
    },
    &error);

// No serialized ECS handles. Source+child names and affine data only.
aiverse::Json saved = host.save(nativeSceneId, objectKey);
aiverse::Json wholeSceneAttachments = host.saveScene(nativeSceneId);

// Resolve a freshly created child Entity every time after reload:
aiverse::Entity torch = host.resolveChild(
    nativeSceneId, objectKey, "left-hand-tool");

// Render on the presentation owner thread after exactly ONE parent
// Animator/Graph evaluation; temporary read leases cover submission:
aiverse::view::ImportedAttachmentAuthoring::Prepared frame;
if(host.prepare(nativeSceneId,objectKey,
                modelProvider.resources(), parentWorld, frame, &error)) {
    frame.object.draw.pose=&alreadyEvaluatedParentPose;
    frame.object.draw.nodeWorld=&alreadyEvaluatedNodeWorld;
    genericView.submitOne(renderer,frame.object);
}

// On native scene unload after the final render submission:
host.unload(nativeSceneId);
// ALWAYS clear before visualWorld or the ModelResourceCache dies.
host.clear();
```

`activate` allocates the host's map entry **before** ECS writes; it calls
the existing transactional `EcsAttachmentScene::restore` and validates
the published parent rig, all enabled child Models, socket bindings and
Model cache leases before structural ECS edits. Replacing a scene object
with a different presentation parent creates the new child generation
first; the old instance is detached only on accepted publication.
`deactivate(scene,object)` drops one specific prefab attachment instance.
`unload(scene)` forgets all owned instances of that scene, never deleting
host-owned parent Entities or foreign game entities. `saveScene(scene)`
exports deterministic ordered object/layout data by stable key.

## SceneLoadCoordinator integration boundary (peer branches)

The actual scene loader is under:

- `feature/scene-runtime-multiworld` —
  `engine/scene/SceneLoadCoordinator.h`,
  `SceneAsset.h`, `SceneRuntime.h`
- `feature/ecs-static-prefab-scene-integration` —
  `engine/scene/PrefabStaticSceneBinding.h`

Those files are NOT currently present in the isolated
`feature/universal-ecs-animation-pipeline` branch. Their source was
read for contract alignment, but **no merge or code changes were made
to either scene branch**.

`SceneLoadCoordinator::SpawnPrefab` currently uses
`bool(World& authoritative, Entity spawned, const SceneObjectAsset& asset,
std::string& error)`, and `SceneObjectAsset` stores a stable scene-local
`uint64_t id`. When both branches are reviewed and integrated, the
native scene/visual host can adapt this callback by mapping the scene
instance ID + `std::to_string(asset.id)` to its **separate visual parent**
Entity and attachment JSON in `asset.properties`. Call
`SceneAttachmentHost::activate` ONLY at the presentation owner/structural
barrier after assets are published. Do not assume that the authoritative
`spawned` Entity belongs to the visual World. On scene unload or
replacement, call `host.unload(sceneId)` before retiring Model resources.
On cold save/restore create fresh visual parent Entities, then call
`activate` using the saved pointer-free attachment layout.

This compatibility plan is **NOT** yet an integrated automatic scene
loader binding; that requires a reviewed cross-branch integration
worktree and actual scene lifecycle tests.

## Animation and GPU ownership

This system is for static child Model attachments rendered by their
parent's evaluated socket pose. It does NOT animate another child
AnimationGraph. A child with its own graph must have a distinct stable
source ID and exactly one independent presentation player. The
scene/child authored IDs provide the identity needed for that future
workflow but do not yet instantiate the player.

`EcsAttachmentScene` and `SceneAttachmentHost` own only presentation
metadata, World child ECS components and immutable compiled socket
bindings. They do not own Model, texture, GPU buffers, animation clocks,
device fences or authoritative simulation state. The native host owns
ModelAssetProvider/ModelResourceCache and must hold short-lived read
leases through submission and defer GPU destruction until actual
completion.

No full ModelAssetProvider token-based shutdown exists yet: its
ModelResourceCache and image rollback shutdown still require a
full-GPU-idle proof. Bypassing it would be unsafe.

**NOT RUN:** CMake/native compiler, CTest, Studio/ModelView/scene runtime,
Metal/Vulkan/GLES, GPU-fence fault injection, sanitizer and performance.
See [Stage 160 handoff](agent-handoffs/animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md).
