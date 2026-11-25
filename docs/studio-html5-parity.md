## Stage 18 — native FX snapshot capture (2026-11-23; QA deferred)

The independent SpectraFX Graph GPU preview has a one-shot **Capture
Frame** action with native RHI readback, bounded 256/512/768px JPEG
thumbnail, Save JPEG and async capture status. There is no continuous
GPU-to-browser bitmap streaming. The existing authenticated MCP tool
`fx_graph_preview` supports `{"action":"capture","maxSide":512}`
and `{"action":"captureStatus"}`, with results under
`capture:{pending,ready,width,height,mime,image?,error?}`.
The native host enforces 128–768px max dimension, 600 KiB JPEG,
one in-flight capture, ten-second timeout and callback-safe
project-switch/close behavior. No gameplay ECS writes or untrusted
disk path. **Current source intentionally not built or tested per
user request; previous 2/2 CTest applies to older source only.**
Agent handoff: `docs/agent-handoffs/spectrafx-studio-stage18-native-gpu-frame-capture-2026-11-23.md`.

## 2026-11-23 — SpectraFX Studio Stages 15–17 (features only; tests postponed)

Added production-native preview-authoring controls to the independent
FX viewport and existing `aiversemcp` `fx_graph_preview` tool:

- **Stage 15**: `playback {speed:0.25|0.5|1|2|4,loop:boolean}`
  uses whole fixed 1/60s simulation ticks, bounded quarter-tick
  accumulator and native-frame restart at 3600 ticks. `stage`
  selects `background:dark|neutral|light|black` and boolean grid/axes;
  `cameraPreset` uses perspective/front/side/top with a separate FX
  camera. All supported through the HTML5 Graph pane and MCP.
- **Stage 16**: opt-in `Auto-reload graph` recompiles an immutable
  in-memory Graph v2 after 650ms debounce. Save Draft/Publish remain
  explicit; native compiler rejects invalid candidate graphs without
  replacing last-good state. Successful recompilation restores Play.
  Native backend initialization failure is a known separate hazard,
  not a proven atomic swap.
- **Stage 17**: `burst {count:1..8}` queues bounded native one-shot
  emitter events (max 32 queued), `clear` defers particle reset to
  the next native device frame. HTML5 Trigger Burst/Clear controls
  and real burst/queue counters; no gameplay/ECS writes.

Docs:
`docs/agent-handoffs/spectrafx-studio-stage15-playback-stage-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage16-auto-reload-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage17-burst-events-2026-11-23.md`.

**User deliberately deferred ALL testing until later.** No exact-SHA
TypeScript/Vite/AppleClang/Metal/Windows/Vulkan/interactive GUI PASS
has been observed for Stages 15–17. Do not describe the source
implementation as hardware-verified. Prior 2/2 Mac CTest PASS belongs
to an older Scene-only preview.

# HTML5 Studio full functional migration

## 2026-11-23 — Stage 13: independent Scene and FX GPU swapchains

The SpectraFX Graph native GPU preview **NO LONGER BORROWS OR
REPOSITIONS the map Scene drawable**. StudioHost now uses three
`StudioSurface` values: `Scene`, `Game` and `Fx`. The native
`Fx` viewport owns its own `StudioScene`, renderer, camera, gfx Device
and native CAMetalLayer (macOS) / Win32 Vulkan child window (Windows).
Both map Scene and FX are framed on the **same render-owner thread**
using separate swapchains and can be displayed simultaneously.
`studio.viewportRect {id:"fx"}` places ONLY the FX surface. The
browser never draws fake particles or receives GPU handles.
`studio.spectraPreview` and `aiversemcp fx_graph_preview` remain
the shared playback/camera/parameter control surface, but target
`fxPreview_` rather than the authored Scene. No native map brush,
selection, asset drop, keyboard deletion or ECS mutation is permitted
from the dedicated FX window. Independent map Scene camera/history
are untouched. Both renderer/device lifetimes retire on the native
owner thread.

This supersedes Stage 11's **one shared native surface** limitation
without replacing the established Stage 11 HTML5 dock rectangle.
Metal CTest source now creates two real devices and captures different
GPU frames; macOS Studio CI also compiles the full native host.
**Full AppleClang/Metal, Windows Vulkan and WKWebView UI acceptance
not yet observed as PASS.** Required agent handoff:
`docs/agent-handoffs/spectrafx-studio-stage13-independent-gpu-targets-2026-11-23.md`.


Owner scope confirmed 2026-11-20: complete full functional transfer before main merge. Do not equate a registered tab, JSON endpoint, placeholder, disabled control or source scan with feature parity. Legacy ImGui stays available until the acceptance matrix is complete. Update 2026-11-21 (stages 103–111): every registered legacy window has an HTML5 panel; the status column says how deep each one goes.

Baseline: main 076139d12d9ea2380a5445f823323f4e2c096811 integrated into feature/studio-html5; tested HEAD 6e54c2e5ad743caa88daa95e4355daf313f942e4. Native app builds, 17 selected CTests pass, Python contracts 19 pass, frontend build/domain tests pass. These checks do not demonstrate functional parity. Mac UI was initially blocked by locked desktop; Stage 80 later exercised native Game interactively.

## SpectraFX V2 editing update (Stage 7, 2026-11-23)

The `fx_graph` HTML5 pane has a typed Curve/Gradient key editor for
existing `Curve` and `Gradient` value nodes, in addition to the
Data Interfaces inspector, real project Assets image picker and native
Workspace-backed topology canvas. Static curve/gradient visualization is
**not** a GPU particle preview. Typecheck/build and macOS/Windows
interaction QA for the latest code remain pending on stacked draft PR #115.
See `docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`.

## SpectraFX Stage 8 update — real native Scene preview (source only)

The `fx_graph` panel now has typed Play/Pause/Step/Seek controls
for native `studio.spectraPreview`. It loads an unsaved v2 Graph through
native compilation, steps a real `GraphRuntime` on StudioScene's
render thread and draws particles before PRISMA resolves the native
Scene pass. The browser renders only controls, never fake particles.
This does not yet create an independent embedded preview viewport or
prove Metal/GPU image parity. Stage 8 macOS native, Windows, WebView
and frontend checks are pending. Handoff:
`docs/agent-handoffs/spectrafx-studio-stage8-native-preview-2026-11-23.md`.

## 2026-11-23 — SpectraStudio embedded native FX preview + MCP (Stages 11–12)

The latest HTML5 SpectraFX Graph panel includes a **real embedded native
Metal/Vulkan particle viewport**: `SpectraDockViewport.tsx` reports its
scroll-clipped CSS rect as `studio.viewportRect {id:"fx"}`. StudioHost
repositions its existing Scene GPU surface with FX priority and switches
StudioScene into a neutral grid stage with isolated camera, preserving the
authored map camera and blocking all Scene entity edits. The real
`spectra::GraphRuntime` executes on the render thread, then draws inside
native GPU render passes. Scene and FX share one surface; concurrent
independent rendering is NOT supported in this initial stage.

`fx_graph_preview` is now in the existing `aiversemcp` tools/list,
sharing a render-thread callback with `studio.spectraPreview`.
Supports status/load/play/pause/step/seek/restart/parameter/stop plus
cameraReset/cameraOrbit/cameraZoom. `load` accepts safe graphName,
`draft:true`, or inline Graph v2. All earlier `fx_graph_*` asset CRUD,
editor and draft MCP tools remain unchanged. Standalone MCP reports no
native GPU viewport and does not fake frames.
Node clipping tests, native Metal isolated dock tests and MCP discovery,
safe path/callback tests were AUTHORED. No exact-SHA AppleClang/Metal/
HTML5/WebView/Windows PASS has been observed for these latest changes;
the user's prior native CTest 2/2 PASS predates the embedded viewport.

Handoffs:
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`

## Window inventory

Derived from actual AIVERSE_EDITOR_WINDOW registrations in apps/editor/windows. Forwarding files without registrations are excluded. Each source remains the detailed behavior reference, including context menus, shortcuts and native-only workflows.

| Legacy ID | Reference | HTML5 status |
| --- | --- | --- |
| `agent` | `apps/editor/windows/AgentWindow.cpp` | Agent panel: Claude Code over the agent link, change list, Undo all, screenshots (109/110) |
| `animation_assets` | `apps/editor/windows/AnimationAssetsWindow.cpp` | Dedicated humanoid role mapping, validation and guarded file save/reopen verified on macOS. Box3D ragdoll generation, body and joint inspection and guarded file save/reopen verified in a detached Mac window. Root Motion bakes and inspects per-tick position and yaw. A detached Mac window saved and reopened a track. Montage edits section transitions, clip ranges, slot, bone mask, blends and source-time notifies. Browser and native operations verified a saved cycle with two events. Detached macOS Montage added a second Release notify, saved, reopened and validated both sections. |
| `animation_graph` | `apps/editor/windows/AnimationGraphWindow.cpp` | Visual pose nodes and wiring, graph scopes, state outputs, transitions and conditions, draft undo, native model validation, and revision-checked saves. Independent skinned playback in the Graph panel was verified on macOS (24f0f304b); Windows playback acceptance remains open. |
| `animation_rig` | `apps/editor/windows/AnimationRigWindow.cpp` | Ordered rig units with native presets, nested parameter editing, contact setup, graph validation and save. Workspace operations available. Add and remove effectors, joints, legs, feet, capsules, balance masses and obstacles. Mac desktop verification covered joint selection and save (00edbc14a), physical IK target dragging (94fa265d1) and skinned playback in the Rig panel (24f0f304b). |
| `assets` | `apps/editor/windows/AssetsWindow.cpp` | Partial: GUID catalog/import/moves/conflict handling/import settings merged and native-tested (78–79); visual and full legacy parity pending |
| `animation_attachments` | `apps/editor/windows/AttachmentAuthoringWindow.cpp` | Attachments edits both transforms and previews the parent, socket and static or MotionWeave child on its own native surface. Mac docked layouts and failed reimport verified. |
| `branding` | `apps/editor/windows/BrandingWindow.cpp` | Dedicated icon editor and native per-platform variants. Detached macOS window saved an adaptive foreground. Splash settings, native launch overrides, sequence JSON, play/pause/restart and scrubbing use the native CPU frame renderer inside Branding. Verified in a detached Mac window and after reopening the project. |
| `build` | `apps/editor/windows/BuildWindow.cpp` | Build panel: toolchain, estimate, build, log (103) |
| `console` | `apps/editor/windows/ConsoleWindow.cpp` | Mac activity and build output verified. Windows verification pending |
| `fields` | `apps/editor/windows/FieldsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene field brush (111) |
| `fog` | `apps/editor/windows/FogWindow.cpp` | Fog of War panel, same controls (108) |
| `FpsWindow` | `apps/editor/windows/FpsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) (fps + LENS). Simulation now edits tick rate, snapshot rate and player limit with guarded rules saves, verified on macOS. Movement edits walk and sprint speeds in actor.motor, verified on macOS. Loadout edits damage and magazine for the legacy single slot and ordered slot lists, verified on macOS. Axis-aligned collider bounds edit in Inspector with map validation, Undo, Redo and Save verified on macOS. Oriented collider Inspector edits center, yaw in degrees and full XYZ dimensions, verified on macOS. AppKit Scene mouse events verified top-view uniform scaling followed by yaw rotation, Undo/Redo and serialized collider data on macOS. Multiplayer launches independent AiverseGame listen and join windows on the selected map. Verified two native processes with an established loopback WebSocket on macOS. Joining at the default UDP address displayed player controls with server ticks advancing from 849 to 1041 on the second project map. |
| `effects` | `apps/editor/windows/FxWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `game` | `apps/editor/windows/GameWindow.cpp` | Partial: native shared GameClient Play map/menu/Stop, Metal surface, keyboard/mouse, runtime menu pause/resume visually verified (80); editor pause/step/restart verified (81); device dimensions/orientation/safe area/touch verified (82); runtime inspection, FPS and device chrome still pending |
| `generate` | `apps/editor/windows/GenWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `hierarchy` | `apps/editor/windows/HierarchyWindow.cpp` | Partial: map entity create/duplicate/delete, single selection/search, undo/save/reopen tested (Stage 69); document type and owner/type grouping, filtered group selection and collapsed Shift range verified in a browser. Live list pagination, filtered queries and retained selection exercised with 213 native simulation entities and a browser panel. A detached macOS Live Hierarchy selected two entities in a hosted match. A newly detached Inspector restored the first and followed the second. Project-defined Edit groupings verified in detached macOS Hierarchy with nested script and owner fields and filtered group selection. Play project grouping verified in detached Mac Hierarchy with live Lua data, nested owner, global counts and retained selection on re-entry. Built-in category/type, origin/type and all-component membership verified in Edit and Play on macOS. Nesting uses document identities and runtime refs. Detached macOS Play verified selecting a summoned companion across parent collapse and expansion. Edit shows script-created preview objects with read-only details, search, Type and Nesting membership. Detached Mac verification retained the selected map parent and identical map bytes. Runtime/FPS authoring remains open |
| `project_import` | `apps/editor/windows/ImportProjectWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) (port_*) |
| `input` | `apps/editor/windows/InputWindow.cpp` | Input panel: InputFlow actions/contexts/bindings via the engine codec (108) |
| `inspector` | `apps/editor/windows/InspectorWindow.cpp` | macOS Inspector shows group scaling for eligible EVOLVE regions. Native persisted two 150% X extents and omitted the command for mixed and ordinary selections. Map page edits the name, grid size, road movement and victory overrides as one MapDoc operation. Detached macOS Inspector saved and validated a resized map. Players edits slots, names, teams, factions, scripted sides, starting resources and flat or planetary coordinates. Scene start picking and Undo on flat and planetary maps verified in the macOS composite viewport. AppKit window events verified first-click placement and drag rejection on the planetary Metal Scene. Areas supports rectangle drawing in the Inspector grid, camera focus, referenced renames and guarded removal. Tiles provides a letter brush and eraser for flat map cells with grouped Undo. Scene displays region boundaries while painting or when enabled in Inspector. A native macOS drag and Undo verified the resulting outlines. Areas accepts a native Scene drag into the active Inspector draft. Native macOS window input and composite WKWebView pointer-event replay verified cancellation, a reverse drag, Undo, Redo and disk persistence. World-space Scene Inspector exposes the scene name and authored collider/spawn counts. Rename, Undo, Redo and lossless disk save verified on macOS. Full native visual parity remains open. Regions in Inspector edits display names, counts cells, previews coverage and arms the selected brush. Verified rename, Undo, Redo, disk save and brush selection in a detached macOS Inspector. |
| `lightbake` | `apps/editor/windows/LightBakeWindow.cpp` | Dedicated HTML settings, quality presets, sampling, advanced options, output statistics and confirmed clear. Mac tested map-scoped Apply, Undo/Redo, save and correct root bake folder with a same-name sibling. Detached Mac atlas preview verified both RGBM irradiance and sun visibility on two engine-baked pages, including refresh after external removal/restoration. The detached display toggle controls the main Metal Scene. Verified baked terrain, a static model, probe lighting for a second placement and live removal/restoration of baked output. The Mac HTML window starts, cancels and forces real background bakes, displays stage progress and reads the resulting atlas. Verified legacy MCP submission and status through authenticated aiversemcp, including settings undo/redo and loaded primitive counts. A Mac layout containing only Hierarchy, Light Bake and Inspector prepared a fresh project with no Scene frame. Verified cancellation, completed output, atlas and unchanged skip. Windows acceptance remains open. |
| `lighting` | `apps/editor/windows/LightingWindow.cpp` | Mac detached Lighting verified spot and beacon editing, conditional frequency and sweep controls, Undo/Redo, map save and reopen. Environment preset inheritance, removal of map overrides, unrelated property preservation, Undo/Redo and stale-map rejection verified on Mac. Point-light color and halo survived independent Undo/Redo, save and reopen on Mac. Standalone Mac game framebuffer verified Snow and Cave palettes saved through Lighting on otherwise identical maps. Mac native HTML verified half-unit placement at the Scene target, selected-light framing, Undo/Redo, stale document rejection and persisted coordinates on both flat and world-space maps. Weather preview renders independently inside Lighting. Mac GPU frames and one-step Apply/Undo verified. Visible browser checked automatic viewport placement, clipping and release; macOS native placement in the capture was explicitly supplied because the locked desktop reported hidden. Windows desktop acceptance remains open |
| `materials` | `apps/editor/windows/MaterialsWindow.cpp` | Dedicated panel-local GPU preview verified on macOS. Material creation writes indexed content/materials/*.material.json assets and Assets reveals a freshly created file. On macOS the detached Materials panel identified a committed file-backed publication and reported the existing material as a rollback blocker. Windows visual acceptance remains open |
| `AnimationWindow` | `apps/editor/windows/ModelAnimationWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110); no timeline view |
| `models` | `apps/editor/windows/ModelWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `ModelingWindow` | `apps/editor/windows/ModelingWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `overlays` | `apps/editor/windows/OverlaysWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `physis` | `apps/editor/windows/PhysisWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `postfx` | `apps/editor/windows/PrismaWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `studio_asset_audit` | `apps/editor/windows/ProjectAuditWindow.cpp` | Asset Audit panel (108) |
| `project` | `apps/editor/windows/ProjectWindow.cpp` | Mac verified searchable map and rules groups, disk-only maps, create/edit, duplicate protection and Rules shortcut. Native map new/open/save/save-as and dirty guard remain covered by Stage 71. Project Hub and Windows have separate verification. |
| `raid` | `apps/editor/windows/RaidWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `roads` | `apps/editor/windows/RoadsWindow.cpp` | Road network plan, node position and height, lane-attached segment creation, plus Scene road drawing and schema tools. macOS verified numeric node editing, adjacent curve control translation, undo/redo and save/reopen with two lane attachments. |
| `rules` | `apps/editor/windows/RulesWindow.cpp` | Partial: unit-type form (health, move, vision, cost, weapons, flags) + raw JSON per type via rules_get/rules_set (stage 100); triggers, damage matrix, globals and the full document are available. Actor projects open the complete rules document even when types is absent. Verified navigation from detached Rules to Actors & Cameras and its Movement settings on macOS. Project provides scoped resource, technology and faction editors. Detached macOS Project rejected malformed JSON, created and renamed a technology and a faction and reopened both after restarting Studio. The technology cost survived the rename. Resource creation and restart were verified separately. Project > Types now inserts all 36 shared Inspector block defaults into a draft. Mac Studio verified malformed-draft handling, existing property protection and persistence after restart. Rules > Movement shows loaded body radius and implicit mass. Editing radius, overriding mass, resetting to automatic and reopening were verified in the macOS host |
| `scene` | `apps/editor/windows/SceneWindow.cpp` | Partial: native rigid-model render/picking/hot reload merged, cache release corrected (78–79); Unity-style navigation + orientation gizmo with axis snap and Persp/Iso (91, web/Linux-checked, Mac build pending); transform gizmos, rect selection, broader authoring/model formats and visual parity pending |
| `scripts` | `apps/editor/windows/ScriptsWindow.cpp` | Mac verified source navigation, live simulation and view logs, error filtering and file-change restart. Starter creation, duplicate rejection and registration after reopening verified in Mac Studio. |
| `shader_graph` | `apps/editor/windows/ShaderGraphWindow.cpp` | Graph editing, wiring, undo/redo, creation and application. Save & apply saves a guarded draft and applies its snapshot. Mac desktop verified material, game and map postfx and particle emitter targets, stale source rejection and retry. Source nodes show their assigned output channels. Detached macOS Shader Graph verified optional emission assignment/unassignment, mandatory color, undo, explicit save and reopen. Embedded native preview for material, postfx and particle drafts. macOS framebuffer readback covers all three domains. Native macOS game frames also verify a saved map-specific postfx override on the second of two maps. Windows visual acceptance remains open. |
| `spectrafx_gpu_assets` | `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `spectrafx_graph` | `apps/editor/windows/SpectraFxGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; full Niagara graph editing and GPU particle preview still pending |
| `spectrafx_graph` | `apps/editor/windows/SpectraGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; full Niagara graph editing and GPU particle preview still pending |
| `stats` | `apps/editor/windows/StatsWindow.cpp` | Statistics panel: map contents, Scene fps (110) |
| `studio_camera_bookmarks` | `apps/editor/windows/StudioCameraBookmarksWindow.cpp` | Scene toolbar bookmarks (106). macOS verified a stored vertical field of view after relaunch and compatibility with earlier HTML bookmarks. macOS migrated all six ImGui views, preserved occupied shortcuts, restored projection and lens, and kept removed shortcuts empty on relaunch. All original views remain available in the submenu. Windows execution remains open. |
| `studio_extensions` | `apps/editor/windows/StudioExtensionsWindow.cpp` | Extensions panel: editor/extensions.lua widgets, commands, Scene gizmos (111) |
| `studio_frame_capture` | `apps/editor/windows/StudioFrameCaptureWindow.cpp` | Frame Capture panel: frame times, draw calls, capture (111) |
| `studio_snapshot_diff` | `apps/editor/windows/StudioSnapshotDiffWindow.cpp` | Snapshot Diff panel (110) |
| `terrain` | `apps/editor/windows/TerrainWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene brush: sculpt, paint, objects (110/111) |
| `texture_resources` | `apps/editor/windows/TextureResourcesWindow.cpp` | Texture Resources panel: live GPU textures, estimate (111) |
| `tiles` | `apps/editor/windows/TilesWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene tile brush (111) |
| `tools` | `apps/editor/windows/ToolsWindow.cpp` | Scene toolbar (Q/W/E/R, snap, pivot, bookmarks) and brushes. macOS verified connect/disconnect of two selected entities in Inspector, history, disk persistence and reopen. A macOS framebuffer comparison verified the road strip between two outposts and its disappearance after disconnecting. On a four-unit ridge the rendered link followed the crest and disappeared on removal. Two macOS launches verified Scene clicks A, Shift-B, C, Escape, undo/redo and reopened A–B and B–C links. WKWebView composited Scene also verified trusted pointer input, drag-out-and-back cancellation, Shift, Escape and saved links while Metal terrain and roads were visible. Windows execution remains open. |
| `water` | `apps/editor/windows/WaterWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene river drawing (111) |
| `static-geometry` | `apps/editor/windows/StaticGeometryWindow.cpp` | Static Geometry lists the configured scene and writes per-instance Auto/STRATA/LOD (47396ae81). Independent native processes verified saving all three modes to a custom scene path and reloading. Live renderer counters and selection decisions remain outstanding. |
| `lens_cameras` | `apps/editor/windows/StudioCamerasWindow.cpp` | Actors & Cameras edits camera records and captures its own native preview to PNG. Mac docked and wide captures verified at two field-of-view settings. Follow presets and Use in Scene verified on macOS. EV100 and depth of field, plus creation from orthographic and perspective Scene cameras, verified on macOS. Windows runtime acceptance remains open. |

**2026-11-25 inventory check.** The original table covered 49 C++ registration files. Scanning apps/editor/windows for lines beginning with AIVERSE_EDITOR_WINDOW found 51, including the two rows above. HTML panel counts measure a different inventory: the workspace redesign checked 59 registered HTML panels at desktop and narrow widths. The acceptance gallery verifies opening and layout. See [workspace verification](handoff/studio-html5/workspace-redesign-2026-11-25.md) for fixture and platform limits.

Later stages added runtime inspection, Scene transforms and marquee selection, a separate animation timeline, Post FX controls and road/river spline gizmos. See [runtime](handoff/studio-html5/83-live-runtime-inspection.md), [Scene](handoff/studio-html5/99-multi-select-marquee-hierarchy-flags-asset-drop.md) and [timeline and splines](handoff/studio-html5/112-spline-gizmos-hub-postfx-timeline-splash.md). Animation Graph has a dedicated visual authoring panel. Rig still routes through ToolPanel. RulesPanel edits unit types, rules-level triggers and the damage matrix (b8250d349). Global settings and complete rules.json editing use native validation and stale-document checks (069d814e8). AttachmentsPanel edits sockets and transforms. See the attachment preview acceptance below.

## Workflow gates and execution order

1. Project/document lifecycle: open/create/switch, map new/open/save/save-as, unsaved-change handling on switch/close/quit, validation failures, reload, persisted settings. Test real files outside source tree.
2. Scene authoring: create/delete/duplicate/rename entities, multiple selection and grouping/search/filter, owner/type changes, hierarchy reparenting and visibility/locks, transform tools, undo/redo, cross-genre document nodes, actual models/materials/terrain rendered through native shared renderer. Existing preview is not full model/render parity.
3. Game lifecycle: actual native play/stop/pause/step/reset, runtime hierarchy and inspector, user input, device simulator and safe transition back to authored document. Launching an external client alone is not full Game-window parity; do not instantiate a second gameplay solver.
4. Data/tools: project/rules/assets/scripts/build/import/branding/audits/console/statistics, existing validated Workspace operations with narrow explicit bridge methods. Never expose unrestricted Workspace::call or filesystem/shell access to web content.
5. World authoring: terrain/tiles/water/roads/fields/fog/overlays/generation/lighting/light bake/FPS/raid tools. Keep existing operations and authored save formats; browser owns presentation only.
6. Rendering/animation: complete materials/shader graphs/PRISMA/SpectraFX/modeling/animation/rig/assets/attachments/texture resources/PHYSIS; preserve owning engine systems, native render previews and domain-specific validation.
7. Workspace ergonomics: docking/menus/shortcuts/context actions/preferences/camera bookmarks/frame capture/snapshot diff/extensions/agent panel; keyboard accessibility, loading/error/empty/disabled states and narrow panes.
8. Acceptance: all rows supported by actual runtime workflows and screenshots, native + frontend checks, existing editor regression protection, save/reopen/undo/redo and failed-operation atomicity, relevant platform checks. Only then merge main and publish validated artifacts. Record any unsupported platform honestly.

## Required evidence per row

Record implementation commit, frontend panel, explicit native methods, normal/error/undo/save behavior, actual automated command and result, screenshot path and any unresolved gap. A command form does not automatically replace a visual graph/timeline/gizmo tool. Multiple real features may share a common typed component, but never hide missing functionality behind a generic placeholder.

## Next bounded stage

Entity CRUD (69), packaging (70), map lifecycle (71), shared game host extraction (72) and remote asset/model/facial-rig convergence (78–79) have scoped automated evidence. Latest combined suite: native 25/25, web 85/85, Python 41/41; targeted recovery/picking follow-up 4/4. New Facial Rig panel is a partial authoring addition, not animation parity. Next: native interactive QA after Mac unlock, real game lifecycle using shared runtime ownership, then remaining matrix. No full-parity or main-merge claim is justified yet.

Facial Rig preview migration: independent model binding, controls and drawable in the Facial Rig panel. Human Creator viewport migration tracked separately.

Character Creator and VIVID Appearance render on separate native surfaces. The Scene frame receives no character binding. The HTML5 workspace hosts every docked and floating panel inside the same responsive page container. Individual visual acceptance is in progress. Windows preview input and photo overlay acceptance remain open.

Animation Graph and Animation Rig each own a native playback session. Their HTML skeleton previews support play, pause, frame stepping, bind pose, three projections and joint selection. On macOS, picking a wrist and applying it to a rig unit persisted the bone name to the graph asset. GPU skinned mesh playback and interactive ground contacts remain open.

Animation preview ground height and solver diagnostics run in the owning panel. On macOS the two-foot rig reported two stable supports at zero and none at −10 without advancing playback. Independent sessions are covered by studio_animation_preview. The live climbing readout still needs an articulated climbing fixture in the desktop app.

Rig vector parameters appear as draggable targets in the skeleton view. Keep controls applies modified values to the selected graph scope as one undoable edit. On macOS a physical drag moved the solved wrist, and Save graph persisted the target while preserving its third component and the pole. Independent skinned mesh rendering remains open.

Skinned preview stage: publish the evaluated native pose and draw its imported model on dedicated Graph and Rig GPU surfaces. Verify an actual weighted mesh in two poses.

Animation Graph and Animation Rig now render the selected skinned model in their own native drawables beside the evaluated skeleton in wide panels. In bundled Mac Studio the weighted arm followed the wrist through 15 frame steps to 0.50 s, with Scene hidden. Both windows were separately opened and captured. Windows runtime and the independent attachment authoring preview remain open.

Attachments stage: preserve both affine transforms while editing, publish draft parent and socket children to the owning HTML window, verify on a real imported model and retain last good publication on failure.

LENS camera authoring stage. Replace schema-only camera collection edits in Actors & Cameras with a responsive record list and native-validated draft editor. Preserve unknown fields, separate project and revision guards, and actual saved-file evidence. Camera output preview and recording require independent runtime acceptance.

Camera navigation parity stage. Cover the Follow-body toggle, native preset list and editable framing, plus the explicit command that takes Scene to the selected camera.

Current camera acceptance covers authoring from the active Scene projection and native physical-lens readouts for finite and infinite focus intervals.

Console in the Mac bundle displayed 49 lines from a successful size estimate. Copy full log matched all 3103 bytes on disk. The Errors filter was exercised on that successful run. A second estimate failed on a deliberate output-path collision in the disposable project. The Errors view showed the native audio copy diagnostic and terminal failure on lines 9 and 11, exit 1. Finder opened the logs window containing editor.log and build-estimate.log. Windows desktop verification remains pending.

Mac Console Activity showed the running VM error at scripts/game.lua:23, Tick 0, with source Lua. The status bar reported one error during play and zero after stop. Capture docs/evidence/studio-console-html5/play-script-error.png.

Verified in Mac Studio. Clicking scripts/game.lua:23 in Console selected the exact source line. Scripts displayed the live simulation VM print and error at tick zero. Errors only hid the print, and Stop returned the log to its empty state. Captures docs/evidence/studio-console-html5/scripts-source-line.png and scripts-runtime-log.png. Script file change detection and opt-in/out restart verified in the Mac bundle.

The native host checks project scripts while the panel is closed and logs changed and removed paths. The desktop test loaded revision A, retained A after saving B with restart disabled, and restarted into C after saving C with restart enabled. Adding a file with invalid Lua syntax produced a clickable line 1 error. Capture docs/evidence/studio-console-html5/scripts-restart-enabled.png.

Mac Studio verified an on_frame failure at scripts/view/studio-play-overlay.lua:2. Console identified the source as Lua view. Scripts selected the exact error statement and displayed the simulation print and view failure together. Errors only retained the view failure and Stop cleared the live error count. Captures docs/evidence/studio-console-html5/view-script-source.png and view-script-log.png.

The Mac app created scripts/main.lua with on_start and unit_died and scripts/view/hud.lua with view.on_frame. A duplicate request reported the collision and preserved the source. The saved rules listed both scripts exactly once. The map ran with zero Lua VM errors. Reopening the project restored both file entries and their correct roles. Captures docs/evidence/studio-console-html5/script-create-form.png and script-created-source.png.

Desktop evidence in docs/evidence/studio-project-catalog. The Mac bundle opened a disk-only map, discovered an externally copied map after Refresh, created and renamed studio.crystal and rejected a duplicate. The renamed item persisted across app restart.

Project Hub now searches the full host-provided recent list by name or folder, marks missing folders, and presents template selection, description and project setup together. Mac desktop created Harbor Study from the empty template in a selected folder and reopened it from filtered recents. Each recent row now supports removing the registry entry. Mac Studio removed Harbor Study from search and the shared registry while game.json and rules.json retained their SHA-256 hashes. The new Game wizard uses the shared native planner and generator. Verified on Mac with local artwork, design notes, template matching, tutorial, three levels, bot checks, native destination selection and opening the generated project. Project, Levels, Economy and Distribution settings now bind to the same generator specification. Cobalt Harbor verified French resources, two levels and a boss, customized currencies/XP and generated store and ad configurations in Mac Studio. The Concept page downloads linked artwork before planning. Mac Studio generated Tide Archive from a ZIP served over local HTTP and showed its original PNG in Assets. The Concept and Economy fields now expose macOS dictation. Field ownership, live updates, stop, insertion, error and cancellation were tested with a simulated recognizer; physical voice acceptance remains open. Generated projects expose WIZARD_TASK.md as an editable draft in Agent, including its detached window. Mac desktop created Willow Sound with the wizard option selected, opened Agent and loaded its 4,694-byte task from the generated project. Copy draft supports clients outside Studio. Agent supports selecting Codex CLI or Claude Code. Mac Studio exercised the Codex start, progress, stop and restart paths with a local fixture. The After creation choice can open the game, review the brief or send it to an available client. On macOS, Emerald Quay generation sent the complete brief to a local Codex fixture through the normal Studio launch path. Its real MCP read/write appended a section to WIZARD.md, visible after reopening the project in Agent’s report viewer. Cancelling the folder picker left the current project open. External provider responses, voice input and Windows remain unverified.

Shader Graph project switching requires restoring the unsaved graph and retaining its original save precondition when the user returns to the project.
Confirmed in a detached Mac Shader Graph window across map changes; two projects with the same shader name and explicit discard verified in the browser bridge fixture.

Lighting transfer: expose authored point/spot patterns, frequencies and searchlight orientation/sweep through the existing light-update API. Verify conditional controls, undo and persistence.

Map environment transfer: expose palette preset and removal of the map lighting block, preserving other properties and native map Undo.

Local light color and halo transfer: verify legacy defaults, range checks, native Undo, map persistence and detached Lighting controls.

Lighting weather preview: dedicated GPU drawable, time/cloud/storm controls, one undoable map override and independent Scene camera.

Lighting placement: add lights at the Scene orbit target on a half-unit grid; retain explicit map-center placement and document-scoped Undo.

Light Bake transfer: map-scoped settings, quality presets and generated output inspection in a dedicated HTML panel. Resolve bake folders alongside the actual map for nested and root map paths. Background baking and renderer consumption require separate native parity verification.

Light Bake migration: extract captured-scene preparation and job execution into the existing CPU bake library so both desktop hosts can submit immutable scene samples. Verify real output, content hash reuse and cancellation before HTML integration.

Light Bake output inspection: display individual baked irradiance and shadow-mask atlas pages inside the HTML panel. Verify native decoding, page switching and isolation from Scene.

Light Bake transfer: the main Scene loads the active planar map’s baked lighting, binds static placements, terrain and probes through Renderer3D, and exposes the legacy visibility toggle. Panel previews keep their own rendering state.

Light Bake transfer: capture the native Studio document placements, terrain and lighting into the background baker input.

Light Bake transfer: connect the map-scoped job to the HTML panel and publish completed outputs.

Light Bake transfer: restore the linked lightbake_bake and lightbake_status operations in HTML Studio, including optional settings and renderer counters.

Light Bake transfer: prepare the map from the document while Scene is closed. Capture must not present a frame or change the layout.

Roads transfer: author road nodes from a map-local network diagram, with revision-checked moves and Undo through road_node_set.
