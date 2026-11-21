# ModelView — real imported socket attachments

This is an **opt-in native model asset preview and authoring workflow** that exercises the shared, genre-independent
`engine/view/ImportedAttachmentAuthoring.h` API. The parent and imported static child models are owned by the same
`render::ModelAssetProvider`; `MotionWeave`/View never interprets a game genre or action.

## Start with real models

```sh
# The parent file must contain the chosen named socket/node.
aiverse_modelview models/actor.glb@Walk \
  --attach models/lantern.glb@Hand.R \
  --attachments-file assets/actor.attachments.json
```

Or start with an existing JSON file:

```sh
aiverse_modelview models/actor.glb --attachments-file assets/actor.attachments.json
```

The JSON is intentionally lightweight, authored by ModelView and independent of any gameplay config:

```json
{
  "attachments": [
    {"model": "models/lantern.glb", "socket": "Hand.R", "enabled": true},
    {"model": "models/backpack.glb", "socket": "Spine", "enabled": false}
  ]
}
```

`--attach child.glb@SocketName` adds extra entries to those loaded from the JSON file.
Attachment preview automatically activates native provider mode and disables primitive merging/baked display.
`--game` gallery mode cannot be combined with this feature; ordinary standalone model previews and gallery
behavior are otherwise unchanged. Multiple parent input models are allowed, but every active named socket
must exist on **each** parent or an edited layout is rejected rather than partially published.

## ModelView UI

The **Attachments** window can toggle an authored child, **Detach** it from the layout,
add a child by specifying its *real imported model file* and named socket,
**Rebuild** the current layout, and **Save** the configuration.
If no `--attachments-file` was specified, Save defaults to
`<first-parent-model-path>.attachments.json`.
Changes apply at the next **GPU/device owner frame boundary**, not inside the immediate draw submission.
An invalid edit keeps the previous compiled attachment layout active and reports an error in ModelView.

The two operations are distinct:
- **Detach** removes an attachment from the scene layout, but does not immediately unload its cache-owned Model
  or evict GPU resources; the provider retains shared residency until safe later retirement/shutdown.
- **Rebuild** recompiles the layout using the provider's **currently resident** model revisions; it is **not**
  an on-disk file hot-reimport. A changed child file still needs a model-source invalidation/republication
  transaction after existing graphics submissions finish. This workflow does not yet expose that transaction.

## Source and lifetime contracts

The parent Animator runs exactly once in ModelView, then the genre-neutral
`ImportedAttachmentAuthoring::prepare(..., AnimationOwner::HostPrecomputed)`
builds one borrowed `View::Object` containing current parent pose/matrices and compiled child sockets.
`View::submitOne` renders parent and children without ticking another animation graph.

The compiled layout holds published immutable Appearance metadata and precompiled socket indices,
**not** permanent ModelReadLeases. Each drawn instance receives a move-only
`ImportedAttachmentAuthoring::Prepared` that owns short-lived CPU read leases for source
and imported child Models (plus optional visual LODs), released after synchronous View submission.
GPU resource retirement still belongs to ModelAssetProvider / ModelResourceCache and explicit
device completion tokens. No high-crowd performance or native correctness claim should be made
until the full test pass.

This Stage 154 functionality is **source implemented only**. CMake build, CTest, actual render,
Metal/Vulkan/GLES, GPU fence failure injection, sanitizer and perf verification have not been run.
Continue from [Stage 154 handoff](agent-handoffs/animation-stage154-modelview-attachment-workflow-2026-11-19.md).


## Stage 157 — full on-disk Model file reimport

ModelView now provides TWO distinct operations:

- **Rebuild** only recompiles the selected attachment layout against Models that
  are currently resident in the ModelAssetProvider, including changes to named
  socket assignments and enabled children.
- **Reload model files** re-reads the parent and EVERY enabled child Model file
  from disk into a NEW, independent native ModelAssetProvider. It stages new
  parent Animators, published appearances and compiled sockets together. A
  failed file upload, missing Model or invalid socket leaves the existing
  visible parent/child scene active. On successful validation it swaps the
  entire visual source generation at the next render-owner frame boundary.

To request reload automatically when an existing file is edited on disk:

```sh
aiverse_modelview models/hero.glb@Walk \
  --attachments-file assets/hero.attachments.json \
  --watch-model-files
```

Or launch without `--watch-model-files`, click **Reload model files** in
the Attachments window whenever source Model files change, or enable
**Watch source files** in the window. Timestamp polling is intentionally
lightweight (once per second, on the render-owner thread). It tracks filesystem
last-modified timestamps of the parent and enabled child paths, not arbitrary
virtual archive URLs. The observer records each changed timestamp once, so a
failed candidate does not cause an unbounded continuous retry storm; modify
the file again or click Reload to retry. The JSON layout itself is still
reloaded using its own normal authoring workflow, not the Model watcher.

For a compatible named animation clip, the parent Animator is re-created
against the NEW Model and its normalized layer-zero clip phase is restored
using `Animator::seek`. If a clip was removed or renamed, that instance
falls back to its new bind pose instead of interpreting an old clip index
on a changed skeleton. World placement and UI variant selection remain in
the viewer.

The OLD provider, old Animator instances and borrowed published Model
snapshots remain alive together in a deferred retirement queue until the
GPU owner has proven all prior submissions completed. An advisory cap of
eight retired source generations triggers a full GPU-idle cleanup, and final
ModelView shutdown also requires a full idle barrier. This is NOT fully
async per-provider GPU token retirement: it can still stall at the cap and
temporarily retain multiple sets of GPU Models/textures. If GPU completion
cannot be proven, old GPU owners are retained instead of being unsafely
destroyed.

The new path has **NOT YET** passed native CMake/C++ compile, CTest,
Metal/Vulkan/GLES visual tests, failure-injection or memory/crowd profiling.
It is a committed source implementation, not a guarantee of live tested
file watching or safe graphics retirement on every backend. See
[Stage 157 handoff](agent-handoffs/animation-stage157-modelview-atomic-model-file-reimport-2026-11-20.md).


A standalone `aiverse_modelview models/hero.glb --watch-model-files` invocation is also supported without any child attachments. It uses the same native provider-backed source reload, with an empty child socket layout.



## Stage 158: shared layout, full affine transforms, parent inferred from JSON

`aiverse_modelview --attachments-file assets/hero.attachments.json` now
opens the saved document's `parent` Model without a separate parent command-line
argument. Source files are still loaded by the normal native provider. The
layout file is parsed by the same genre-neutral `AttachmentLayoutCodec` as
AIVERSE Studio; each child can store optional complete 16-value column-major
`local` and `childWorld` affine matrices. If absent, they are identity.
**Socket offset** X/Y/Z is editable directly in the ModelView Attachments UI
and survives a JSON Save/Load. Rotations, scales and shears may be authored
through the full matrices; Studio exposes the full basis editor.
See [the shared layout schema](attachment-layout.md). Runtime parent/child
scene ECS instantiation and GPU-token provider shutdown are separate work.
Full native compile/CTest/Metal-Vulkan-GLES validation **NOT RUN**.



## Stage 160 — prefab child identity

The Attachments UI now exposes a **Prefab child ID** field for each imported
child. If set, it persists as `"id"` in the shared attachment layout JSON,
is unique within that document (max 128 chars), and may be resolved into the
CURRENT ECS child Entity when hosted in a presentation scene. Legacy entries
without an ID are still accepted but cannot be referenced stably after
reimport/reordering. Duplicate IDs fail layout compilation before importing
Models and fail Save before opening/truncating a previously valid JSON file.
See [SceneAttachmentHost](scene-attachment-host.md). Native GUI/CTest/GPU
validation remains NOT RUN.



## Stage 161 — animated child JSON compatibility

ModelView now preserves the optional `animated` Boolean and
`animationGraph` source key when loading or saving attachments. Enabling
`animated=true` requires a stable child ID and a Graph asset reference.
**Important:** this standalone viewer still has a parent-only Animator,
so an enabled independent animated child is explicitly rejected during
layout compilation instead of incorrectly being submitted as a static
parent socket draw. Use the Studio child Graph preview or the native
`AnimatedSceneAttachmentSources` integration API until ModelView
receives its own child MotionWeave source host. See
[animated child architecture](animated-scene-attachments.md) and
[Stage 161 handoff](agent-handoffs/animation-stage161-independent-animated-child-graph-sources-2026-11-20.md).
No native CMake/CTest/Metal/Vulkan/GLES runtime checks have been run.
