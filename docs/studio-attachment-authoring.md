# AIVERSE Studio — MotionWeave attachment authoring (Stage 155)

The **MotionWeave — Attachments** window lives under the Studio **Look / Graphics** workspace,
is closed by default, and can be opened from the existing Modeling and Animation Graph toolbars
via their **Attachments** buttons. The window is deliberately independent of game genres.

## Author an attachment layout

Open a Studio project, then open **MotionWeave — Attachments**. Specify a project-relative
**Parent Model** file, for example `models/hero.glb`. Add one or more **Child Model**
files and **Socket** names, e.g. `models/lantern.glb` at `Hand.R`. Toggle **Enabled**
for each child or use **Detach** to remove an entry from the authored layout. Press
**Reimport & Publish** to validate and publish the complete source/child model set.

**Save JSON** persists the authored file layout independently of game rules; **Load JSON**
restores it. The path is relative to the project and is checked against parent-directory and
symlink escapes. The file supports the ModelView attachment fields plus the parent path:

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {"model": "models/lantern.glb", "socket": "Hand.R", "enabled": true},
    {"model": "models/backpack.glb", "socket": "Spine", "enabled": false}
  ]
}
```

This is a **file-backed authoring and socket validation window**, not yet a 3D attachment
viewport embedded in the Modeling/Animation Graph panels. Those panels keep their own
`AnimationGraphInstance` preview owner and their own directly-held Model; Studio does
not move or borrow those objects into this window's native ModelResourceCache. To test
actual child rendering, the standalone [ModelView attachment preview](modelview-attachments.md)
continues to use the same generic ImportedAttachmentAuthoring API.

## Model publication and reimport safety

Each **Reimport & Publish** builds a COMPLETE replacement resource session in a new,
dedicated `render::ModelAssetProvider` using the Studio GPU-device generation lease.
Both the parent and all enabled children are uploaded into the **same new cache**;
`AppearanceAssetRegistry` publishes their immutable metadata and
`ImportedAttachmentAuthoring::compile` checks every named socket against its real
parent rig. No existing live source resource is invalidated during staging. A missing
file, malformed model or unknown bone rejects the candidate and leaves the previously
published session active.

Publication deliberately uses a **full GPU idle barrier** before swapping the new
resource session and shutting down the old provider. This is a **blocking authoring**
operation, not zero-stall hot reload. Failure to prove GPU completion retains the
previous session; if old or scratch resources cannot be safely retired they are held
alive rather than destroyed without GPU proof. The project switch and final Editor
shutdown follow the same cache-owner lifetime rule.

A successful operation re-reads the bytes from disk, unlike ModelView's existing
**Rebuild** action (which only revalidates currently cached models). It does not
automatically replace a running gameplay client's assets, copy a Model into a
different owner, evaluate another animation graph or alter any simulation ECS data.
Snapshot pointers and compiled sockets do not own CPU/GPU Models; the provider does.

## Remaining work and verification

Stage 155 introduces the real Studio **asset-authoring integration**, including
file-backed add/toggle/detach/save and safely staged reimport. It does NOT yet
provide a 3D viewport in Studio for these attachments, nonblocking GPU asset
hot reload, editor drag/drop, attach-to-animated-child rigs, or runtime project
prefab/scene serialization. The existing Modeling and Animation Graph windows
only open this dedicated window; they do not share its ModelResourceCache.

**NOT RUN** for this stage: C++ compilation, CMake/CTest, real Metal/Vulkan/GLES
rendering, UI integration, device-fence fault injection, sanitizer or performance
benchmarks. Continue from [Stage 155 agent handoff](agent-handoffs/animation-stage155-studio-attachment-staged-reimport-2026-11-20.md).


### Update scheduling

The **Reimport & Publish** button queues the request; the next Studio
`EditorWindow::update()` executes it on the resource-owner thread before
window drawing, not inside the current ImGui button callback. This is still
blocking once that update begins, and graphics Device-frame legality must be
confirmed on Metal/Vulkan/GLES during the final native validation.


## Stage 156: embedded 3D preview and deferred provider retirement

The Studio attachment authoring tab now includes a **real offscreen 3D viewport**
for the imported source Model and compiled child sockets. Drag the image to
orbit the camera; when the source has a clip, preview the first imported clip
with a Play control and phase slider. The viewport uses its own provider-owned
source/child resource session, the existing Studio offscreen-target retirement
queue, and a render-only `View` submission of a sampled pose. It never
borrows another editor window's Model or evaluates a second AnimationGraph.

Successful Reimport & Publish now swaps to an entirely staged provider session
before GPU-idle retirement. Old provider owners stay alive in a **bounded
deferred queue**; when 8 sessions are retained, a full-idle cleanup is
attempted. GPU completion failure retains resource owners; shutdown requires
a full idle proof. This avoids a mandatory GPU wait on EVERY authoring edit,
but it is NOT fully nonblocking fence retirement, and multiple complete
Models/textures can temporarily occupy VRAM. `ModelAssetProvider::shutdown`
itself still waits for GPU idle. Preview and retirement have NOT yet been
compiled or run on Metal, Vulkan or GLES; see
[Stage 156 handoff](agent-handoffs/animation-stage156-studio-attachment-preview-deferred-retirement-2026-11-20.md).



## Stage 158: common attachment document with affine transforms

Studio Attachments now shares exactly the same `AttachmentLayoutCodec` JSON
format as native ModelView, including full column-major affine `local`
(socket offset/rotation/scale) and `childWorld` (child model-local placement).
Existing files without matrices remain valid with identity transforms. The
editor exposes basic **Socket offset** X/Y/Z and an **Advanced affine
transforms** section for the 3x3 basis of both local/child matrices and child
translation. On publishing the layout, these matrices are applied through
the generic `ImportedAttachmentAuthoring::childFromLayout`, not just
serialized as notes. Invalid/non-affine values fail before replacing the
last-good authored document. See [the shared attachment JSON format](attachment-layout.md)
and [Stage 158 handoff](agent-handoffs/animation-stage158-universal-attachment-layout-transform-2026-11-20.md).
Actual Studio GUI, native compile/CTest and GPU visual/lifetime checks **NOT RUN**.



## Stage 160 — stable child IDs and scene lifecycle preview

Studio attachments now let authors edit an optional **Prefab child ID**
alongside child source and named socket. It is saved/loaded through the
shared `AttachmentLayoutCodec`; duplicates are rejected. Each Studio
preview session uses the real genre-neutral `SceneAttachmentHost`
bound to an independent cosmetic ECS World, with stable preview key
`{1,"preview"}`. The preview prepares visual children only after
validating the current ECS root/child generations. Old session teardown
calls the host's `clear()` before retiring provider-owned Models.

The same host API is ready for adapter use at scene activation/unload
barriers, but it is NOT yet bound to `SceneLoadCoordinator` from other
development branches and does NOT mutate authoritative game ECS state.
See [SceneAttachmentHost guide](scene-attachment-host.md) and
[Stage 160 handoff](agent-handoffs/animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md).
Native Studio/Metal/Vulkan/GLES compiler and renderer validation NOT RUN.



## Stage 161 — independent child Graph authoring in Studio

For any child use **Independent animation** to switch from a static
parent-socket Model draw to a separately evaluated MotionWeave child.
Provide a unique **Prefab child ID**, the actual imported **Child Model**
path, its named source **Socket**, and **Child AnimationGraph file**
(project-relative). Studio's explicit Reimport & Publish stages the
imported child Model and compiles its Graph through the shared
AppearanceAssetRegistry. Invalid Graph assets reject the staged
session, leaving the prior published configuration intact.

The child is still a real presentation ECS Entity but NOT included
in its parent's static socket draw. On every preview frame the Studio's
`EditorWindow::update` advances its independent child MotionWeave
Graph once, then the offscreen Viewport submits graphless precomputed
child skinning poses placed on the parent's animated bone. The parent
has its own separate Animator/clip sampler. This is not a new gameplay
ECS world or a second writer of the same Graph.

ModelView currently keeps these fields in JSON but cannot display
independently animated children; it rejects them. Rich child graph
timeline/montage/IK/morph authoring and GPU-fence-safe full provider
shutdown remain unfinished. See [animated child guide](animated-scene-attachments.md)
and [Stage 161 handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md).
Native compiler/CTest/real Metal/Vulkan/GLES previews NOT RUN.
