# AIVERSE MotionWeave / Universal View — Stage 155 handoff

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT at initial inspection
Previous: [Stage 154](animation-stage154-modelview-attachment-workflow-2026-11-19.md)

## User's mandatory contract

**All MotionWeave animations, the generic View and its reusable model/attachment/retargeting APIs are independent of game genres.** Host/UI code may author Model paths and named sockets; neither animation core nor engine View may import FPS/RTS/RPG/network/gameplay rules. A presented source has exactly one AnimationGraph/pose writer. The user requests **complete functions first, then one comprehensive native Mac test pass**, and **a user-visible readiness table after every implementation stage**.

Stage 154 integrated `ImportedAttachmentAuthoring` with the standalone native ModelView. Stage 155 introduces a REAL Studio `EditorWindow` consumer with its own resource owner and staged source-file reimport. We intentionally do NOT cast/share `ModelingWindow::model_` or `AnimationEditorPanel::model_` into ModelResourceCache: those direct preview Models have different lifetimes. Studio's existing Modeling/Graph preview animation clocks remain untouched. This is an authored-layout compiler/validator, not yet a live 3D attachment preview inside the Studio viewport.

## Exact source commits

1. `37d866bcf1812c037e1aea5528b0980e382376d6` — new `apps/editor/windows/AttachmentAuthoringWindow.cpp`:
   - Uses `AIVERSE_EDITOR_WINDOW(AttachmentAuthoringWindow)`, automatically collected by the existing `file(GLOB AIVERSE_EDITOR_WINDOWS CONFIGURE_DEPENDS ...)` in root CMake. It appears in the `look` group, right dock, default closed, as `MotionWeave — Attachments`.
   - UI edits an explicit project-relative **parent Model** path and `attachments: [{model,socket,enabled}]` child list. Attach, toggle, Detach, Load JSON, Save JSON, Reimport & Publish, with copy-on-success authoring changes. Project-relative paths reject absolute paths, `..` traversal and canonical symlink escapes.
   - A staged `Session` contains a unique `render::ModelAssetProvider`, `AppearanceAssetRegistry`, published parent and children, and a compiled `ImportedAttachmentAuthoring::Compiled`. Parent/child file bytes are read from the live project and uploaded into the **same NEW ModelResourceCache** via ModelAssetProvider; source and children remain host/provider-owned. Socket compilation/validation is performed with the generic library, not Studio-specific animation logic.
   - On `Reimport & Publish`, a COMPLETE independent candidate session is populated before touching the previous session. Any failed file/import/GPU upload/socket check discards the candidate only after definitive GPU idle proof, leaving last-good Studio source session visible. Before committing a successful candidate, the render-owner thread explicitly calls `gfx::Device::waitForResourceIdle()`. The old provider's GPU Models are shut down only after that barrier, and an inability to prove safe retirement preserves a heap-owned old/scratch session rather than destroying potentially in-flight GPU resources. This is intentionally **blocking** Studio authoring, not asynchronous or zero-stall hot reload.
   - On project switch, the old session is safely retired before adopting new project paths and layout. At final Editor shutdown, the Editor already proves a whole-device idle barrier before `windows_.clear()`; the window's destructor still checks full device completion before destroying its provider.
2. `7f76a677ab6705923cf13210f38205e903512b73` — `apps/editor/windows/ModelingWindow.cpp`: button `Attachments` opens the new window via `ed.focusWindow("animation_attachments")`. It does NOT pass its directly-owned GPU Model into another cache.
3. `44237cd8b07a408fa72339747ab869293e6faa7e` — `apps/editor/animation/AnimationEditor.h`: same `Attachments` navigation from the generic Graph/Rig authoring UI. The active `AnimationGraphInstance` remains owned and evaluated only by its existing preview panel.
4. `144dfc2359f5270c9297860a6963054b6f15f70a` — new Studio attachment window also calls `ModelAssetProvider::setGpuDeviceLease(ed.gpuDeviceLease())` on every new candidate provider, guarding against native GPU Device pointer reuse/recreation.
5. `82511aeca57f0f39fcf5088201abc0add0830891` — new [docs/studio-attachment-authoring.md](../studio-attachment-authoring.md) explains UI, example JSON, staged reimport, blocking GPU fence, cache ownership and limitations.

## User workflow and data

Open a Studio project and choose `Look` → `MotionWeave — Attachments`, or the **Attachments** button in Modeling or Animation Graph. Set a project-relative parent Model, add real imported child Model files and named sockets, then click **Reimport & Publish**. Load/Save JSON:

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {"model": "models/lantern.glb", "socket": "Hand.R", "enabled": true},
    {"model": "models/backpack.glb", "socket": "Spine", "enabled": false}
  ]
}
```

The JSON is *source-neutral layout authoring*, not gameplay save/load or serialized GPU pointers. A successful Studio compile displays the current number of validated child sockets; invalid source file, failed GPU upload, unknown named socket or missing Model retains the previous Session and reports an error. Disabled entries are preserved in the JSON but do not import Models or compile socket bindings.

## Resource ownership and safety proof boundaries

- `Session::provider` exclusively owns its imported parent + children, plus texture and GPU handles. `AppearanceAssetRegistry` owns immutable appearance metadata only. `ImportedAttachmentAuthoring::Compiled` holds source/child appearance snapshots and socket plans, not GPU/CPU Model lifetime.
- Socket preparation happens on the owner thread with `ModelResourceCache::acquireModelRead` leases. Those short-lived leases have ended before attempting to retire a provider. They are NOT GPU completion fences.
- A candidate's whole independent ModelResourceCache is built BEFORE touching the old provider; no old cache replacement/invalidation occurs during staging. On publication, `waitForResourceIdle` proves old GPU submissions complete, then the old provider is shut down and source metadata dropped. If the barrier fails, the previous session is kept and the unprovably safe scratch GPU owner is **held alive rather than destroyed**. This is a resource leak *fail-closed fallback*, not desirable production behavior; instrument/fix this during native failure injection.
- Project switch enforces the same GPU idle proof before dropping old Model sources. This window does not render the compiled bundle itself and does not create a second animated ECS World; modeling/graph preview remains a separate preexisting preview owner.
- Actual application wiring is via the existing editor window registry and UI navigation. There is no new CMake target, no gameplay/genre imports in `engine/view`, no permanent ModelReadLease, no root motion/notify/state change, and no Model material schema or serialization changes.
- **Important limitations:** Studio's attachment tab currently compiles/persists real imported assets, but has **NO in-Studio 3D child rendering, no drag/drop/gizmo transform editing, no scene/prefab writeback**, and uses full GPU idle blocking on each explicit Reimport. `aiverse_modelview` still uses its existing cached `Rebuild` path (not on-disk reimport) and its own render preview. Running app/plugin correctness has NOT been confirmed with a compiler.

## What was actually verified

**ACTUAL:** read connected GitHub sources of Stage 154 handoff, ModelResourceCache's owner-read/invalidate APIs, ModelAssetProvider, ImportedAttachmentAuthoring, Editor.h/Editor.cpp shutdown, EditorWindow registry, Studio Modeling/Graph/Rig windows, and root CMake editor window auto-registration. Used SHA-guarded connected GitHub writes to create the Studio attachment editor and button links, set a GPU Device generation lease, write its user guide, and read back the changed source locations. No existing game or animation ECS core code was modified; source-neutral generic imported attachment API was reused as-is.

**NOT RUN:** native CMake configure, C++ compile/link, current passing CI, CTest, actual Studio GUI/ModelView launch, real Metal/Vulkan/GLES rendering, GPU fence failure injection, read-lease concurrency, ASan/TSan/UBSan, memory/VRAM/crowd benchmarks, or `bash tools/animation/validate-final.sh`. No claim this compiles or fully works at runtime.

## Readiness matrix after Stage 155

| System | Source status | Remaining |
| --- | --- | --- |
| MotionWeave ECS / genre-neutral AnimationGraph | Implemented incrementally | Native worker and pose tests |
| Graph hot reload / named playback | Implemented incrementally | Failure injection and reimport parity |
| Stable logical source and Model epochs | Implemented incrementally | Cross-host checks / real reuse tests |
| Whole-model Animation LOD | Implemented in shared View | Real imported rigs and performance |
| Retargeting and morph mapping | Partial | Complex skeletal hierarchies, morph authoring |
| Compiled sockets / imported static child models | Shared core exists | Real visual/scoped GPU validation |
| ModelView Attach/Detach/JSON UI | Stage 154 source exists | Actual render + source-file hot reimport |
| Studio Attach/Detach/JSON authoring tab | Stage 155 source exists | In-Studio 3D preview / C++ compile/UX |
| Studio staged full on-disk asset reimport | Stage 155 source exists | Runtime/fence failure tests and nonblocking workflow |
| Studio Model/AnimationGraph preview parity | Partial | Common attachment viewport integration |
| Metal/Vulkan/GLES | NOT VERIFIED | Backend visuals and fence tests |
| Complete native suite and sanitizers | NOT RUN | One final coordinated Mac pass |

"Implemented" means committed code and GitHub readback, not a tested feature.

## Next Stage 156

1. Add a **true in-Studio visual attachment viewport** using the staged Studio Session's own provider cache or a safe shared draw-owner boundary, without borrowing raw Models from ModelingWindow. Keep one source animation writer; allow prepared pose preview only through correct host ownership.
2. Replace Studio's blocking full-idle reimport with a properly deferred GPU retirement/fence queue when it can be proven to coexist with other Studio draws, while preserving old last-good bindings on failed asset load.
3. Add source-file refresh/reimport to ModelView (currently only layout Rebuild), plus safe rejection of stale source/visual caches.
4. Continue universal complex skeleton/morph retarget authoring, any missing attachment/LOD parity and editor scene/prefab persistence. None of these belong in a genre-specific MotionWeave core.
5. Only after genuine feature closure, coordinate a disposable integration worktree and run the user's one full native Mac test suite/Metal/Vulkan/GLES/ASan-TSan benchmarks; fix compiler/runtime issues before moving PR #22 from Draft. Maintain one stage-specific handoff and user-visible readiness table every stage. No auto-merge, rebase or force-push of concurrent branches.


## Stage 155 follow-up — pre-draw owner-update dispatch

- `6074a10c7c8f6d859db7568c01db0fb7f9b32303` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`: UI `Reimport & Publish` now sets `pendingReimport_` only. The subsequent Studio `EditorWindow::update()` on the render owner thread performs full staged resource upload, socket compilation and explicit GPU idle proof **before drawing windows**. On project change, stale pending reload requests are dropped. This avoids invoking a potentially blocking GPU wait from the middle of the active ImGui attachment window draw.
- Source inspection confirmed `Editor::frame` calls all window `update` hooks before `ui_.newFrame()` and any `window::draw` hooks. It does **not** independently prove whether the platform has already begun its graphics Device frame before calling `Editor::frame`; confirm GPU backend semantics during final native validation. No background work, native test or nonblocking completion claim is made.
