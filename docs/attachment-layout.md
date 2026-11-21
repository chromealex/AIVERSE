# AIVERSE universal attachment layout format — Stage 158

The shared CPU-only header `engine/view/AttachmentLayout.h` defines the
**genre-independent**, version-neutral attachment document used by
AIVERSE Studio and ModelView. It can also be embedded as an attachment
subdocument in any future scene/prefab format: no FPS/RTS/game logic,
GPU handles, animation state or ECS entity pointers are serialized.

## File format

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {
      "model": "models/lantern.glb",
      "socket": "Hand.R",
      "enabled": true,
      "local": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0.12,-0.03,0.05,1],
      "childWorld": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    }
  ]
}
```

The matrices are **column-major, 16 numeric values**, matching
`render::mat4::m[col*4 + row]`. `local` is relative to the named source
skeleton socket. `childWorld` is an additional child-local transform, so
the final model placement is

```cpp
child.world = parent.world * boneModelSpace * local * childWorld;
```

The source bone is always evaluated on the **base animation skeleton**,
regardless of which visual whole-model LOD is rendered. An absent `local`
or `childWorld` means identity, so older files with only
`model`, `socket`, and `enabled` remain valid.

Arbitrary affine rotations, translations, nonuniform scales and shears are
preserved without forcing a lossy TRS decomposition. Perspective matrices,
invalid array sizes, nonnumeric / infinite elements or extreme values are
rejected, leaving the caller's previous document unchanged.
`AttachmentLayoutCodec::write` validates each matrix before serialization.

## Native APIs

```cpp
#include "engine/view/AttachmentLayout.h"
#include "engine/view/ImportedAttachmentAuthoring.h"

aiverse::view::AttachmentLayout document;
aiverse::view::AttachmentLayoutCodec::read(Json::parse(json), document);
const Json serialized = aiverse::view::AttachmentLayoutCodec::write(document);

// After a ModelAssetProvider has imported a real child Model and the
// AppearanceAssetRegistry has published its static appearance:
auto child = aiverse::view::ImportedAttachmentAuthoring::childFromLayout(
    document.attachments.front(), childAppearanceSnapshot);
```

The compiler does not own any file, Model, renderer or texture resource.
Callers supply their own path sandboxing, import transactions, GPU device
generation/read leases, error handling and atomic file writes.

## Editor integration

AIVERSE Studio uses the shared parser/writer for Load/Save and passes each
stored matrix to the actual compiled `ImportedAttachmentAuthoring` socket
layout. The Studio UI has a simple draggable socket-position offset and an
advanced editor for all three affine basis vectors and child-local offset.
ModelView uses the same shared parser/writer and provides a simple
per-socket translation-offset control; the complete matrices can also
be authored in JSON.

To open a saved document in ModelView without a separate parent argument:

```sh
aiverse_modelview --attachments-file assets/hero.attachments.json
```

This opens the document's `parent` Model and enabled child sources in the
standalone native provider owner. `--watch-model-files` remains opt-in.

The layout document is **reusable for future scene/prefab authoring**,
but the current scene/prefab ECS serialization does NOT automatically
instantiate or restore these attachments. That integration remains an
explicit unfinished feature.

## Ownership, animation, and verification

The JSON does not serialize runtime source IDs, compiled socket indexes,
CPU Model pointers, renderer GPU handles, graph state or in-flight fences.
Imported assets are resolved at runtime, precompiled against their current
publication epochs, and protected by existing short-lived ModelReadLease
guards during synchronous render submission. Each animated source still has
exactly one graph/pose writer.

This Stage 158 source change has **NOT** been compiled or tested on a native
backend. Full CMake/CTest, Studio/ModelView visual parity, Metal/Vulkan/GLES,
file roundtrip/negative test cases, sanitizers and crowd benchmarks remain
scheduled for the user's coordinated final validation pass. See
[Stage 158 handoff](agent-handoffs/animation-stage158-universal-attachment-layout-transform-2026-11-20.md).



## Stage 160 — optional stable prefab child ID

Every attachment entry can now include `"id": "left-hand-tool"`.
A nonempty ID is a stable authored child key within its parent prefab
(maximum 128 characters, unique among entries). Both
`AttachmentLayoutCodec::read` and `write` reject duplicate IDs and wrong
JSON types before changing an existing authored document. An omitted
`id` preserves Stage 158 legacy positional layout behavior.

Studio and ModelView preserve this field during Save/Load and offer a
**Prefab child ID** input. `EcsAttachmentScene::resolveChild` /
`SceneAttachmentHost::resolveChild` resolve the CURRENT Entity generation
from an authored key after reimport; old Entity handles cannot be persisted.
For a whole scene, `SceneAttachmentHost::saveScene(sceneId)` exports the
ordered object keys and pointer-free layouts. Refer to
[SceneAttachmentHost contract](scene-attachment-host.md) and
[Stage 160 handoff](agent-handoffs/animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md).
These features are source-only; native compilation and runtime tests
have NOT been run.



## Stage 161 — independent child animation source

A child may opt into its OWN compiled AnimationGraph with
`"animated":true` and `"animationGraph":"graphs/lantern.animation.json"`.
An enabled animated entry requires a nonempty unique stable `"id"`.
Older JSON files without these fields remain static parent-pose sockets.
A newly animated child is still represented by a scene-owned ECS child
Entity, but is NOT drawn in the parent's static socket packet: its own
`AnimatedSceneAttachmentSources` player evaluates an independent local
Pose and composes its placement from the parent's named bone and stored
affine matrices. Each Graph is updated ONCE; View receives a graphless
precomputed child draw.

Studio can compile its child Graph from the authored file and preview
the result. Standalone ModelView preserves these JSON fields but
currently rejects enabled animated children, rather than rendering
them twice or treating their Graph as static. See
[animated child API and caveats](animated-scene-attachments.md) and
[Stage 161 handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md).
Native CMake/CTest/Metal-Vulkan-GLES and real animation tests NOT RUN.
