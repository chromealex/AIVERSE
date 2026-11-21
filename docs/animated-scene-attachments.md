# AIVERSE MotionWeave — independently animated child attachments

**Stage 161.** A child attached to a parent skeleton can now be either:

- a **static Model attachment** following the parent's already evaluated
  pose at a named socket (Stages 150–160); or
- an **independently animated child** with its **own** published Model,
  compiled `AnimationGraph`, stable scene/object/child source identity,
  separate MotionWeave presentation-ECS player and current evaluated pose.

Both paths are completely **genre-neutral**. No game simulation, physics,
networking or RTS/FPS/RPG action logic enters the shared runtime.

## Authoring format

Use the existing `AttachmentLayoutCodec` JSON schema and opt in per child:

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {
      "id": "lantern",
      "model": "models/lantern.glb",
      "socket": "Hand.R",
      "enabled": true,
      "animated": true,
      "animationGraph": "graphs/lantern.animation.json",
      "local": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0.1,0,0,1]
    },
    {
      "id": "backpack",
      "model": "models/backpack.glb",
      "socket": "Spine",
      "enabled": true
    }
  ]
}
```

An enabled independently animated child **must** have a nonempty
stable `id` and a nonempty `animationGraph` asset path. Optional
`animated` defaults to false, so all previous attachment documents
remain compatible. The complete source-model/child-model transforms,
including `childWorld`, are unchanged from Stage 158.

An animated child is **never compiled or submitted as a static
`ImportedAttachmentAuthoring` socket draw**. Instead, it becomes
one animation source managed by `AnimatedSceneAttachmentSources`.
A child without its own animation Graph remains on the previous efficient
static attachment path.

## Native source API

```cpp
#include "engine/view/SceneAttachmentHost.h"
#include "engine/view/AnimatedSceneAttachmentSources.h"

// SceneAttachmentHost already has a live cosmetic World, stable
// scene/object/child keys and a published parent appearance/Model.
aiverse::view::AnimatedSceneAttachmentSources animated;
using ChildInput=aiverse::view::AnimatedSceneAttachmentSources::Input;
std::vector<ChildInput> inputs{
  {{sceneId, "hero-42", "lantern"}, publishedLanternAppearance, {}}
};

aiverse::view::AnimatedSceneAttachmentSources::Statistics stats;
std::string error;
if(animated.sync(sceneHost, provider.resources(), inputs,
                 dt, stats, &error)) {
  // The parent Animator/Graph has ALREADY evaluated a pose and bone matrices.
  aiverse::view::AnimatedSceneAttachmentSources::Prepared child;
  if(animated.prepare(sceneHost, provider.resources(),
       {sceneId, "hero-42", "lantern"}, parentModelDraw,
       parentNodeWorldMatrices, child, &error)) {
    // The Graph has been evaluated in the child host's presentation World.
    // The prepared View::Object.graph is NULL: never call View::update on
    // this object, or run the same child graph a second time.
    childView.submitOne(renderer, child.object);
    // Keep the move-only child.read leases alive through actual draw.
  }
}
```

`publishedLanternAppearance` is a real
`AppearanceAssetRegistry::Snapshot` with immutable `model`,
nonzero `modelPublicationGeneration`, and a compiled
`graph` whose `graph->model()` matches that model. The native host
must publish it BEFORE `sync`, keep the imported Model and GPU resources
alive, and ensure both parent and child belong to the same
`ModelResourceCache`. Native projects may publish such an appearance
via `AppearanceAssetRegistry::Request.animationGraph`,
`graphReader` and nonzero `sourceId`.

`AnimatedSceneAttachmentSources::sync` owns a dedicated
`MotionWeaveSourceBridge` and separate `World`. Call it **once per
presentation frame** at the structural owner barrier with the COMPLETE
set of active animated child inputs. It validates each scene-child
identity, authored Graph mode, source/child Model epochs and cache read
leases. Each active child has its own monotonic source ID, with the
**current generation-aware child ECS Entity** serving as its logical
generation. A changed child Entity on reload causes a controlled graph
restart, while the stable scene/object/child key survives in the host.
Missing inputs retire their child graph players.

After this single update, `prepare` obtains fresh parent/child
`ModelReadLease` objects, composes the independently animated child's
world matrix using the parent's current socket bone position, and assigns
the child's evaluated pose from its own MotionWeave player. It clears
`View::Object.graph`, so the generic View is ONLY the renderer here.
No independent child Graph update is performed by `View`.

Animated child Graph and Pose storage reside in the host's presentation
World and must not be mutated while its borrowed prepared draw is in use.
The model provider/host is still responsible for GPU fences and safe
parent/child source retirement; a CPU ModelReadLease is not a GPU fence.

## Studio

AIVERSE Studio's `MotionWeave — Attachments` window now exposes
**Independent animation** and **Child AnimationGraph file** per child.
During `Reimport & Publish`, it imports the real child Model and compiles
its authored graph through the existing `AppearanceAssetRegistry`
`animationGraph` + `graphReader` contract, then publishes the
scene attachment layout with its static and independently animated items
separated. Missing Graph files or compiler errors reject the staged
resource session and preserve the last-good published session.

The Studio owner-thread `EditorWindow::update` calls the independent
child host's `sync` once per preview frame (or with `dt=0` when paused)
BEFORE any ImGui/Renderer3D submission. The 3D viewport later draws
the parent and static children through the existing presentation View,
plus a separate graphless child draw for every active animated source.
No second Graph evaluation occurs in those draw calls.

The Studio's parent animation preview still samples its first imported
clip. This is not full animation montage/Control Rig/morph/timeline
authoring for the child. A running gameplay `SceneLoadCoordinator`
adapter also remains to be integrated after reviewed scene-branch work.

## ModelView limitations

Standalone ModelView now **preserves and roundtrips**
`animated`/`animationGraph` authoring fields. It intentionally rejects
an **enabled animated child** with an explicit error rather than silently
drawing it statically with the parent's Animator: ModelView has no
separate SceneAttachmentHost/child MotionWeave player yet. Use the Studio
preview or a native host implementing the above source API. Importantly,
this prevents a misleading double-render or accidental second updater.

## Verification and open work

These changes are committed source code, not a native-tested release.
**NOT RUN:** CMake/C++ build, CTest, Studio/ModelView GUI execution,
scene load, actual animation screenshots, Metal/Vulkan/GLES, GPU fence
injection, sanitizers or high-crowd performance.

The broader runtime still lacks automatic SceneLoadCoordinator integration
on the animation branch (scene files live on other branches), genuine
tokenized whole ModelAssetProvider GPU retirement, a ModelView independent
child player, and comprehensive advanced morph/rig authoring.
See [Stage 161 agent handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md).
