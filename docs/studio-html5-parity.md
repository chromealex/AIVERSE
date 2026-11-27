## SpectraFX legacy layout

Published v1 graph upgrades should preserve the legacy canvas arrangement through draft save and reopen.

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
| `assets` | `apps/editor/windows/AssetsWindow.cpp` | GUID catalog, import, moves, conflict handling and import settings native-tested (78-79). Packaged macOS verified project-root navigation, folder search, Unicode selection, keyboard and context-menu path copying, opening folders and returning to content on reveal. Narrow and wide layouts verified in Chromium. Windows acceptance pending. |
| `animation_attachments` | `apps/editor/windows/AttachmentAuthoringWindow.cpp` | Attachments edits both transforms and previews the parent, socket and static or MotionWeave child on its own native surface. Mac docked layouts and failed reimport verified. |
| `branding` | `apps/editor/windows/BrandingWindow.cpp` | Dedicated icon editor and native per-platform variants. Detached macOS window saved an adaptive foreground. Splash settings, native launch overrides, sequence JSON, play/pause/restart and scrubbing use the native CPU frame renderer inside Branding. Verified in a detached Mac window and after reopening the project. |
| `build` | `apps/editor/windows/BuildWindow.cpp` | Build panel: toolchain, estimate, build, log (103) |
| `console` | `apps/editor/windows/ConsoleWindow.cpp` | Mac activity and build output verified. Windows verification pending |
| `fields` | `apps/editor/windows/FieldsWindow.cpp` | macOS desktop verified field selection, single-cell radius, signed fractional values on two player layers, Shift erase, Undo and saved-map reopen. |
| `fog` | `apps/editor/windows/FogWindow.cpp` | Fog of War panel, same controls (108) |
| `FpsWindow` | `apps/editor/windows/FpsWindow.cpp` | Actor panel with LENS cameras and actor operations. Simulation now edits tick rate, snapshot rate and player limit with guarded rules saves, verified on macOS. Movement edits walk and sprint speeds in actor.motor, verified on macOS. Loadout edits damage and magazine for the legacy single slot and ordered slot lists, verified on macOS. Axis-aligned collider bounds edit in Inspector with map validation, Undo, Redo and Save verified on macOS. Oriented collider Inspector edits center, yaw in degrees and full XYZ dimensions, verified on macOS. AppKit Scene mouse events verified top-view uniform scaling followed by yaw rotation, Undo/Redo and serialized collider data on macOS. Multiplayer launches independent AiverseGame listen and join windows on the selected map. Verified two native processes with an established loopback WebSocket on macOS. Joining at the default UDP address displayed player controls with server ticks advancing from 849 to 1041 on the second project map. |
| `effects` | `apps/editor/windows/FxWindow.cpp` | Dedicated effect and event bindings file editor. Mac desktop verified creating bindings, validation, conflicting writes, reload and reopening comment-preserving source after restart. Templates and imports use the operations section. Dedicated fx/*.json Metal viewport verified real moving particles, 0.1–2× speed, replay and last-good playback with an invalid draft. The locked Mac required the test to publish the measured panel rectangle. Effects camera orbit, zoom, travel limits, reset and replay framing verified by Mac keyboard interaction and Metal frame comparison. Mac desktop verified draft emitter add, duplication, deletion, 1–16 UI limits, validated save, preserving source comments and reopening saved blend, alignment, minimum quality and burst. Loop and shape controls use the same editor. Nine emission/motion range fields support constants, random bounds, and single or paired curves. Mac desktop verified implicit bounds, reversed signed ranges, per-key edits through chronological display, comment preservation and persistence. Mac verified sphere/cylinder force fields, selected-emitter influence and native rendered response to a constant directional force. Mac verified scheduled bursts, compact source migration retaining comments, cycles, chance and count curves. Real isolated Metal playback followed 0 → 7 → 14 particles over two scheduled emissions. Mac verified conditional shape dimensions, mesh source, transforms, arc sampling, save/reopen and distinct point/donut Metal playback. Lifetime RGBA gradients, random between two ramps and native save/reopen verified on Mac. Spawn pair, emitter-time gradients, second gradient and speed-dependent RGBA modulation verified with native save/reopen and distinct Metal colors. Desktop Mac verified per-axis initial size and mesh rotation, aspect fallback, texture, gravity, local space, prewarm and capped ring buffer authoring across restart. Remaining emitter modules, physical pointer input and Windows acceptance remain open. |
| `game` | `apps/editor/windows/GameWindow.cpp` | Partial: native shared GameClient Play map/menu/Stop, Metal surface, keyboard/mouse, runtime menu pause/resume visually verified (80); editor pause/step/restart verified (81); device dimensions/orientation/safe area/touch verified (82); 49-profile searchable chooser with dimensions, density and retained orientation verified on macOS; 0.25–16× local Game speed, pause/step/resume and restart verified on macOS; standalone AiverseGame project menu and independent client exit verified on macOS; player-specific local map launch, bots-only spectating and restart verified on macOS; live entity inspection verified in stage 83; paused Metal device frame, oriented camera island and safe-area guides verified in both orientations; Windows Vulkan child overlay implemented and cross-compiled, pending Windows visual/input acceptance; hosted first-person play on a second saved map, pause, step, resume, restart and stop verified in packaged macOS Studio with Scene closed |
| `generate` | `apps/editor/windows/GenWindow.cpp` | Mac desktop verified dedicated generation controls, exact adjacent seeds above 2^53, repeatable terrain, Undo of a bake, fixed/match seeds, disable, Redo and save/reopen. |
| `hierarchy` | `apps/editor/windows/HierarchyWindow.cpp` | Partial: map entity create/duplicate/delete, single selection/search, undo/save/reopen tested (Stage 69); document type and owner/type grouping, filtered group selection and collapsed Shift range verified in a browser. Live list pagination, filtered queries and retained selection exercised with 213 native simulation entities and a browser panel. A detached macOS Live Hierarchy selected two entities in a hosted match. A newly detached Inspector restored the first and followed the second. Project-defined Edit groupings verified in detached macOS Hierarchy with nested script and owner fields and filtered group selection. Play project grouping verified in detached Mac Hierarchy with live Lua data, nested owner, global counts and retained selection on re-entry. Built-in category/type, origin/type and all-component membership verified in Edit and Play on macOS. Nesting uses document identities and runtime refs. Detached macOS Play verified selecting a summoned companion across parent collapse and expansion. Edit shows script-created preview objects with read-only details, search, Type and Nesting membership. Detached Mac verification retained the selected map parent and identical map bytes. Runtime/FPS authoring remains open |
| `project_import` | `apps/editor/windows/ImportProjectWindow.cpp` | Mac desktop verified Godot scan, invalidation, scale validation, conversion, opening the destination and preserving an existing Agent draft when opening the imported project with Agent. |
| `input` | `apps/editor/windows/InputWindow.cpp` | Input panel: InputFlow actions/contexts/bindings via the engine codec (108) |
| `inspector` | `apps/editor/windows/InspectorWindow.cpp` | macOS Inspector shows group scaling for eligible EVOLVE regions. Native persisted two 150% X extents and omitted the command for mixed and ordinary selections. Map page edits the name, grid size, road movement and victory overrides as one MapDoc operation. Detached macOS Inspector saved and validated a resized map. Players edits slots, names, teams, factions, scripted sides, starting resources and flat or planetary coordinates. Scene start picking and Undo on flat and planetary maps verified in the macOS composite viewport. AppKit window events verified first-click placement and drag rejection on the planetary Metal Scene. Areas supports rectangle drawing in the Inspector grid, camera focus, referenced renames and guarded removal. Tiles provides a letter brush and eraser for flat map cells with grouped Undo. Scene displays region boundaries while painting or when enabled in Inspector. A native macOS drag and Undo verified the resulting outlines. Areas accepts a native Scene drag into the active Inspector draft. Native macOS window input and composite WKWebView pointer-event replay verified cancellation, a reverse drag, Undo, Redo and disk persistence. World-space Scene Inspector exposes the scene name and authored collider/spawn counts. Rename, Undo, Redo and lossless disk save verified on macOS. Full native visual parity remains open. Regions in Inspector edits display names, counts cells, previews coverage and arms the selected brush. Verified rename, Undo, Redo, disk save and brush selection in a detached macOS Inspector. |
| `lightbake` | `apps/editor/windows/LightBakeWindow.cpp` | Dedicated HTML settings, quality presets, sampling, advanced options, output statistics and confirmed clear. Mac tested map-scoped Apply, Undo/Redo, save and correct root bake folder with a same-name sibling. Detached Mac atlas preview verified both RGBM irradiance and sun visibility on two engine-baked pages, including refresh after external removal/restoration. The detached display toggle controls the main Metal Scene. Verified baked terrain, a static model, probe lighting for a second placement and live removal/restoration of baked output. The Mac HTML window starts, cancels and forces real background bakes, displays stage progress and reads the resulting atlas. Verified legacy MCP submission and status through authenticated aiversemcp, including settings undo/redo and loaded primitive counts. A Mac layout containing only Hierarchy, Light Bake and Inspector prepared a fresh project with no Scene frame. Verified cancellation, completed output, atlas and unchanged skip. Windows acceptance remains open. |
| `lighting` | `apps/editor/windows/LightingWindow.cpp` | Mac detached Lighting verified spot and beacon editing, conditional frequency and sweep controls, Undo/Redo, map save and reopen. Environment preset inheritance, removal of map overrides, unrelated property preservation, Undo/Redo and stale-map rejection verified on Mac. Point-light color and halo survived independent Undo/Redo, save and reopen on Mac. Standalone Mac game framebuffer verified Snow and Cave palettes saved through Lighting on otherwise identical maps. Mac native HTML verified half-unit placement at the Scene target, selected-light framing, Undo/Redo, stale document rejection and persisted coordinates on both flat and world-space maps. Weather preview renders independently inside Lighting. Mac GPU frames and one-step Apply/Undo verified. Visible browser checked automatic viewport placement, clipping and release; macOS native placement in the capture was explicitly supplied because the locked desktop reported hidden. Windows desktop acceptance remains open |
| `materials` | `apps/editor/windows/MaterialsWindow.cpp` | Dedicated panel-local GPU preview verified on macOS. Material creation writes indexed content/materials/*.material.json assets and Assets reveals a freshly created file. On macOS the detached Materials panel identified a committed file-backed publication and reported the existing material as a rollback blocker. Windows visual acceptance remains open |
| `AnimationWindow` | `apps/editor/windows/ModelAnimationWindow.cpp` | Model inspection and edit operations. The separate AnimationTimeline plays and scrubs authoring-model clips with a skeleton preview. Packaged macOS Model Animation selected two imported clips, sought and sampled visibly different poses, played, paused and opened a clipless asset with Scene closed. |
| `models` | `apps/editor/windows/ModelWindow.cpp` | Model operations plus MaterialTechniquePanel and StrataAnalysisPanel. Original project-file preview resolves the selected type or a relative file, uses a dedicated native surface and excludes project sidecars; verified on macOS with orbit, invalid path, unchanged assets and Scene closed. Packaged macOS Studio also renders configured GameView types with player color and idle clip. Paused pose, two owners, resume and return to original were verified. |
| `ModelingWindow` | `apps/editor/windows/ModelingWindow.cpp` | ModelGraphPanel opens by default inside ToolPanel. Native modeling operations are also available. Packaged macOS Studio opened Attachments from the active model and restored the CSG draft, Undo/Redo and guarded save on return. |
| `overlays` | `apps/editor/windows/OverlaysWindow.cpp` | Shared template and asset operations; independent placement footprint and selectable ground-overlay map plan with GameOverlays geometry, forced anchors, authored colors and Hierarchy selection. Linked overlay_preview opens the HTML panel, shares selection and placement with it, handles partial updates and clears the preview on off. macOS Studio also renders resolved HUD text, entity health bars and screen shapes in the plan. Verified right-edge HUD resizing, selection changes, circle and rounded rectangle pixels. The panel renders the planar game minimap with project background, terrain, player objects and authored marks. Verified ring/disc samples, cursor ping movement and toggling off in macOS WKWebView. On macOS, pixel checks verified a rotated loose texture, a later primitive above it, a camera-facing icon and frame, and a tinted HUD region from a packed atlas. An unavailable texture was reported alongside a valid icon. The Footprint / 3D model switch renders the selected structure through GameView::drawGhost into an independent GPU target. macOS verified the allowed/blocked tint change, selecting another structure, return to footprint, unchanged map bytes and the complete composited model. Windows execution remains open. |
| `physis` | `apps/editor/windows/PhysisWindow.cpp` | Mac verified the private diagnostic world, deterministic reset, +1/+60 ticks, 14 native fields, palette selection, wind/storm/gravity outlines, cell inspection and bounded views without Scene. Authored map operations remain available. Mac verified selected-cell wind and gravity vectors at cell elevation, tick matching, keyboard/pointer parity and missing cells. Mac verified local play-test ECS capture with native Game running beside PHYSIS, paired raster and six cell components on one tick, refresh after resume, stopped-source clearing and independent demo state. Mac verified native map creation, editing, duplication and removal of wind/storm/gravity zones, validation, Undo/Redo and reopen. A panel-local footprint plan supports selection and one-click X/Z positioning; Mac verified preserved Y, one Undo/Redo and side-by-side layout. Desktop Mac verified group selection, translate, yaw and scale about a custom pivot, duplicate/remove, group Undo and disk persistence. The detached Metal nature viewport renders the current-map diagnostic world, supports camera orbit, zoom and terrain ignition, and applies grass quality and volume independently of Scene. See studio-physis-detached-detail.json. Linked MCP activation, private simulation commands, PNG capture and read-only local game presentation are verified on Mac. Local opt-in field submission is implemented. Native fixture verified queued and confirmed application; packaged Mac verified the built-in module refusal. Map zones also exposes the project-host authenticated server route. The loopback integration exercised submit, receipt loss, same-ID retry and confirmed application. Packaged Studio verified offline controls. Packaged macOS PHYSIS draws the selected field and volume outlines on its own Metal surface. Paused game and private world retained independent settings. [Overlay evidence](evidence/studio-physis-viewport-overlay.json). Active storm and gravity contours were isolated by toggling outlines on the same paused tick, with a future storm excluded. [Metal capture evidence](evidence/studio-physis-storm-gravity-rings.json). |
| `postfx` | `apps/editor/windows/PrismaWindow.cpp` | PostFxPanel edits game and map chains, effect parameters and quality tiers, estimates cost and applies presets. Game and map camera-volume bounds, profile editing, isolation, Undo/Redo, invalid/stale rejection and persistence verified on macOS. Packaged Mac Studio verified native Scene camera placement and catalog-based sparse effect overrides with extended RGB values. Per-camera frame reports verified in packaged macOS Studio for Game, Scene and an independent LENS preview, including encoded/skipped effects, source switching and teardown. Packaged macOS verified four preview quality levels, Scene-only visibility, matching native Game and Scene effect reports and linked view_postfx. Game and map bytes matched. [Runtime verification](evidence/studio-prisma-viewport-tiers.json). |
| `studio_asset_audit` | `apps/editor/windows/ProjectAuditWindow.cpp` | Asset Audit panel (108) |
| `project` | `apps/editor/windows/ProjectWindow.cpp` | Mac verified searchable map and rules groups, disk-only maps, create/edit, duplicate protection and Rules shortcut. Native map new/open/save/save-as and dirty guard remain covered by Stage 71. Project Hub and Windows have separate verification. |
| `raid` | `apps/editor/windows/RaidWindow.cpp` | Dedicated extraction point form, objectives, bot seed and player receipts. Packaged macOS Studio verified point creation, invalid edit isolation, Undo/Redo, save/reopen and a bot extraction receipt. Advanced raid operations remain accessible. |
| `roads` | `apps/editor/windows/RoadsWindow.cpp` | Road network plan, node position and height, lane-attached segment creation, plus Scene road drawing and schema tools. macOS verified numeric node editing, adjacent curve control translation, undo/redo and save/reopen with two lane attachments. |
| `rules` | `apps/editor/windows/RulesWindow.cpp` | Partial: unit-type form (health, move, vision, cost, weapons, flags) + raw JSON per type via rules_get/rules_set (stage 100); triggers, damage matrix, globals and the full document are available. Actor projects open the complete rules document even when types is absent. Verified navigation from detached Rules to Actors & Cameras and its Movement settings on macOS. Project provides scoped resource, technology and faction editors. Detached macOS Project rejected malformed JSON, created and renamed a technology and a faction and reopened both after restarting Studio. The technology cost survived the rename. Resource creation and restart were verified separately. Project > Types now inserts all 36 shared Inspector block defaults into a draft. Mac Studio verified malformed-draft handling, existing property protection and persistence after restart. Rules > Movement shows loaded body radius and implicit mass. Editing radius, overriding mass, resetting to automatic and reopening were verified in the macOS host |
| `scene` | `apps/editor/windows/SceneWindow.cpp` | Native rendering, picking and hot reload (78–79). Transform gizmos, snap and multi-selection marquee are implemented (98–99). macOS Move → History → undo and Frame All were exercised (106). Flat Scene axes, object bounds and the primary selection box were independently verified on macOS by comparing rendered frames (studio-scene-diagnostic-layers). Packaged macOS Scene view handlers verified flat-map marquee replace, Shift-add, Command-toggle and empty-area clearing against the live Hierarchy. The receipt uses AppKit event delivery directly to the hit-tested view. System-level input routing, broader model coverage and Windows acceptance remain open. |
| `scripts` | `apps/editor/windows/ScriptsWindow.cpp` | Mac verified source navigation, live simulation and view logs, error filtering and file-change restart. Starter creation, duplicate rejection and registration after reopening verified in Mac Studio. |
| `shader_graph` | `apps/editor/windows/ShaderGraphWindow.cpp` | Graph editing, wiring, undo/redo, creation and application. Save & apply saves a guarded draft and applies its snapshot. Mac desktop verified material, game and map postfx and particle emitter targets, stale source rejection and retry. Source nodes show their assigned output channels. Detached macOS Shader Graph verified optional emission assignment/unassignment, mandatory color, undo, explicit save and reopen. Embedded native preview for material, postfx and particle drafts. macOS framebuffer readback covers all three domains. Native macOS game frames also verify a saved map-specific postfx override on the second of two maps. Windows visual acceptance remains open. |
| `spectrafx_gpu_assets` | `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` | Packaged macOS verified creating a GPU emitter, editing its execution mode and capacity, binding it to an event, route quality, GPU frame budget and spawn scale, canonical validation, guarded save and reload. GPU functions and import/export remain available. Dedicated Metal viewport verified unsaved GPU and CPU twin playback with Scene closed, pause, exact step, restart, recovery and camera. Automatic detached placement, modal occlusion and resuming after dismissal verified in packaged macOS Studio. Windows acceptance remains open. |
| `spectrafx_vfx_graph` | `apps/editor/windows/SpectraFxGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; Mac verified explicit local v1 upgrade, discard, two output branches retained, draft and published conflict protection and reopen. Further graph workflows and Windows acceptance pending Packaged Mac verified module property forms, a side-by-side inspector, function library import with undo/redo and revision-guarded export. |
| `spectrafx_graph` | `apps/editor/windows/SpectraGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; Mac verified explicit local v1 upgrade, discard, two output branches retained, draft and published conflict protection and reopen. Further graph workflows and Windows acceptance pending Packaged Mac verified module property forms, a side-by-side inspector, function library import with undo/redo and revision-guarded export. |
| `stats` | `apps/editor/windows/StatsWindow.cpp` | Statistics panel: map contents, Scene fps (110) |
| `studio_camera_bookmarks` | `apps/editor/windows/StudioCameraBookmarksWindow.cpp` | Scene toolbar bookmarks (106). macOS verified a stored vertical field of view after relaunch and compatibility with earlier HTML bookmarks. macOS migrated all six ImGui views, preserved occupied shortcuts, restored projection and lens, and kept removed shortcuts empty on relaunch. All original views remain available in the submenu. Windows execution remains open. |
| `studio_extensions` | `apps/editor/windows/StudioExtensionsWindow.cpp` | Extensions panel: editor/extensions.lua widgets, commands, Scene gizmos (111) |
| `studio_frame_capture` | `apps/editor/windows/StudioFrameCaptureWindow.cpp` | Frame Capture panel: frame times, draw calls, capture (111) |
| `studio_snapshot_diff` | `apps/editor/windows/StudioSnapshotDiffWindow.cpp` | Snapshot Diff panel (110) |
| `terrain` | `apps/editor/windows/TerrainWindow.cpp` | Scene sculpt, paint and object brushes with terrain operations. Mac verified a native dunes stamp with rotation, hardness, seed, one Undo and saved subcell terrain geometry on reopen. Sparse sculpt editing and Caverns parity require further verification. |
| `texture_resources` | `apps/editor/windows/TextureResourcesWindow.cpp` | Texture Resources panel: live GPU textures, estimate (111) |
| `tiles` | `apps/editor/windows/TilesWindow.cpp` | Tileset and paint operations plus Scene tile and region brushes. macOS WKWebView palette and Scene strokes verified explicit tile 7 and resolved mask16 tile 14, Undo/Redo and save/reopen for loose and packed atlases. |
| `tools` | `apps/editor/windows/ToolsWindow.cpp` | Scene toolbar (Q/W/E/R, snap, pivot, bookmarks) and brushes. macOS verified connect/disconnect of two selected entities in Inspector, history, disk persistence and reopen. A macOS framebuffer comparison verified the road strip between two outposts and its disappearance after disconnecting. On a four-unit ridge the rendered link followed the crest and disappeared on removal. Two macOS launches verified Scene clicks A, Shift-B, C, Escape, undo/redo and reopened A–B and B–C links. WKWebView composited Scene also verified trusted pointer input, drag-out-and-back cancellation, Shift, Escape and saved links while Metal terrain and roads were visible. Windows execution remains open. |
| `water` | `apps/editor/windows/WaterWindow.cpp` | Scene river brush and water operations. Mac desktop verified the selected segment’s type, preserved its adjacent manual beach and opposite bank across rebake and reopen, reset only the selected river, and restored its manual segments via Undo. |
| `static-geometry` | `apps/editor/windows/StaticGeometryWindow.cpp` | Static Geometry lists the configured scene and writes per-instance Auto/STRATA/LOD (47396ae81). Independent native processes verified saving all three modes to a custom scene path and reloading. Packaged macOS Studio showed the active Game renderer’s source counts and per-model decisions for a forced STRATA model and a forced LOD model. Stop cleared the report. Before Play, the native macOS Scene renders the static models and the panel shows its routing. Both authored mode changes updated the renderer, and removing/restoring the instances changed the Scene pixels. On world-space maps the panel edits the active map descriptor. Two maps with a shared instance ID verified file isolation, rejection after switching, an unconfigured map and reopening. Packaged macOS Game with Scene closed reported the two different actor maps’ STRATA/LOD sources, cleared on switching and stopped cleanly. The grid project reported its own two sources in the same build. |
| `lens_cameras` | `apps/editor/windows/StudioCamerasWindow.cpp` | Actors & Cameras edits camera records and captures its own native preview to PNG. Mac docked and wide captures verified at two field-of-view settings. Follow presets and Use in Scene verified on macOS. EV100 and depth of field, plus creation from orthographic and perspective Scene cameras, verified on macOS. Windows runtime acceptance remains open. |

**2026-11-25 inventory check.** The original table covered 49 C++ registration files. Scanning apps/editor/windows for lines beginning with AIVERSE_EDITOR_WINDOW found 51, including the two rows above. HTML panel counts measure a different inventory: the workspace redesign checked 59 registered HTML panels at desktop and narrow widths. The acceptance gallery verifies opening and layout. See [workspace verification](handoff/studio-html5/workspace-redesign-2026-11-25.md) for fixture and platform limits.

Later stages added runtime inspection, Scene transforms and marquee selection, a separate animation timeline, Post FX controls and road/river spline gizmos. See [runtime](handoff/studio-html5/83-live-runtime-inspection.md), [Scene](handoff/studio-html5/99-multi-select-marquee-hierarchy-flags-asset-drop.md) and [timeline and splines](handoff/studio-html5/112-spline-gizmos-hub-postfx-timeline-splash.md). Animation Graph has a dedicated visual authoring panel. Rig opens AnimationGraphPanel in rig mode with ToolPanel for advanced operations. RulesPanel edits unit types, rules-level triggers and the damage matrix (b8250d349). Global settings and complete rules.json editing use native validation and stale-document checks (069d814e8). AttachmentsPanel edits sockets and transforms. See the attachment preview acceptance below.

The 16 former operation-panel rows were compared with StudioPanelRenderer, ToolPanel, toolPanels.ts, StudioTools.h and the corresponding legacy windows at ed1c61ab0. Verification tasks in those rows describe remaining evidence to collect. The overlay preview fallback is a confirmed runtime dependency on the old editor.

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

Overlays transfer: share authored terrain sampling and preview a structure footprint against the open map inside the Overlays panel. Forced overlay rendering remains a separate acceptance item.

Overlays transfer: draw selected ground overlays on an independent plan using GameOverlays anchor evaluation and geometry emission. Verify authored colors, forced selection and placement.

Overlays transfer: bind the legacy overlay_preview command to document-scoped HTML panel state, including partial changes, off and linked MCP calls.

Overlays transfer: emit screen anchors and resolved text, bars, circles and rectangles from the game renderer for the independent HTML Studio plan.

Overlays transfer: render an independent minimap using the game terrain, object and mark rasterizer. Verify project backgrounds, ring/disc coverage and map coordinates in desktop Studio.

Overlays transfer: render authored screen, ground and billboard icon textures in the private plan. Verify loose images, atlas-only regions, tint, orientation and paint order.

Overlays transfer: capture the project structure through GameView::drawGhost into the panel’s own GPU target, with validity tint and snapped map position. Verify blocked and buildable placements with Scene closed.

Game transfer: expose the shared device catalog dimensions and pixel density in a searchable Game device chooser. Verify selection and orientation preservation on macOS.

Game transfer: expose fractional local simulation speed in the Game toolbar, retain the existing fast-forward presets and verify pause, single step and restart.

Game transfer: launch the saved project in a separate AiverseGame process from the HTML Game panel. Verify project identity in the standalone client and independent Studio lifetime.

Game transfer: choose a playable map seat or watch bots in hosted local play. Check actual view-script identity, replayable seat setup, restart and map isolation.

Game device acceptance: show the oriented safe-area distances, keep physical screen corners and camera cutouts opaque and verify both orientations over a paused Metal game.

Windows Game device chrome: layer a click-through hardware mask and safe-area guide on the native Game child. Verify premultiplied pixels in both orientations, independent input and window lifetime.

Game transfer: expose local first-person map play in the Game panel and verify actor pause, step and restart on the selected map.

Fields transfer: expose the selected player layer in the Scene brush and apply Shift erase per dab. Verify layer isolation and Undo.

Tiles transfer: browse the authored atlas, paint a fixed tile, and return to autotiling through the Scene brush. Verify saved map overrides and Undo.

Water transfer: list the selected body’s shore segments by bank and edit their types from the project catalog. Verify rebake, reset, Undo and saved-map reopen.

Generate transfer: bake a map, increment its seed, and enable or remove match-start generation without replacing the baked terrain. Verify Undo, persistence and native seed precision.

Project Import transfer: scan an external project, carry its source into a background import, open the emitted destination and prepare an editable Agent brief. Verify a real conversion and existing drafts.

Effects authoring stage: select a project particle effect or event bindings, edit the source with native validation and guard saves against stale files. GPU particle playback remains open.

Effects playback stage: drive the engine ParticleSystem from the current effect source in a dedicated Effects surface, with replay, motion, looping, quality and playback speed.

Effect camera stage: route drag and wheel input on the Effects native layer to its particle camera on Mac and Windows, with keyboard controls and reset in HTML. Verify distinct camera poses and unchanged source files.

Effect emitter stage: edit emission settings and add, duplicate or remove emitters in the current source draft; preserve other modules and comments. Verify persistence and the engine limit.

Effect ranges: inspect nine emitter parameters, edit constant and random bounds and curve keys in existing source drafts. Verify native validation, source preservation and reopened values.

Effects force fields: edit effect-space field geometry and forces in the draft and the selected emitter’s external force multiplier. Verify add/remove, bounds, save/reopen and preserved unrelated source.

Effects emission stage: schedule timed particle bursts in the selected emitter, retaining compact [time,count] and full sources and using the range/curve editor for the particle count. Verify save/reopen and playback.

Emitter geometry stage: shape-specific dimensions, mesh source, arc sampling and source-space transforms in the HTML effect draft. Verify conditional controls, native persistence and independent Metal preview.

Effects lifetime color: edit RGBA stops, normalized particle age and optional second gradient in a visual inspector. Verify native save and reopen.

Effects color modules: transfer constant, pair and gradient spawn sampling and speed-driven gradient authoring. Verify native save/reopen.

Effects base emitter transfer: edit separate size/rotation axes, sprite aspect, gravity, space, texture, prewarm and particle limits. Verify native files and mode switches in the packaged Studio.

PHYSIS diagnostics transfer: reuse the isolated native demo, tick stepping, all field layers and palette conversion. Provide panel-local map, volume contours and cell inspection. Verify reset and project isolation.

PHYSIS cell details: sample wind velocity and gravity acceleration at the selected native cell center and elevation. Check keyboard and mouse selection against the displayed frame tick, including missing cells.

PHYSIS live transfer: select the local play-test, capture a committed native field raster with optional cell vectors in the same read, and verify isolation from demo stepping and the authored map.

PHYSIS authored zones: move native create, duplicate, edit and remove operations into a dedicated HTML form. Verify revision guards, Undo and saved map reopen.

PHYSIS map zone plan: show native authored footprints and select or position a zone in X/Z, preserving Y. Verify map hit testing and one Undo.

PHYSIS group transforms: select several authored zones, translate, rotate and scale around native pivot modes, duplicate or remove as one MapDoc transaction. Verify Undo, isolation and persistence.

Raid window: provide an extraction point form, readable objectives and bot receipts. Validate candidate map overrides before committing undoable edits. Verify in packaged Studio.

PostFX transfer: edit local camera volume bounds and effect profiles in game and map scopes. Verify that map edits survive reopening and respect Undo.

PostFX volume profiles: catalog-driven parameter overrides, inheritance, and placement from the native Scene camera pose. Verify authored masks and persisted coordinates.

PostFX diagnostic stage: expose native per-view execution receipts for Scene, published LENS and Game in HTML Studio. Check actual encoded effects and stopped-source clearing.

Terrain stamp transfer: use single-click detail sculpt stamps with rotation, hardness, seed and terrain elevation rendering in Scene. Verify one stroke, Undo and persistence.

Sparse Terrain transfer: expose chunk creation and signed-world sculpting in the HTML Terrain brush. Check compound undo, reversed depth, camera placement and persisted chunks.

Terrain target height: transfer the signed Set brush and one-click Scene surface sampler. Verify that sampling preserves the document and history, then verify painting and persistence.

Terrain grid transfer: choose automatic, base-cell or detail-sample routing for sculpt and material strokes. Check layer isolation, base material erase, undo and persistence.

Terrain base-surface inspection: show and pick the editable corner surface while an explicit base brush is armed in the map Scene. Check return to detail on disarm and that inspection never rewrites either layer.

Planetary Terrain transfer: pick the visible body mesh, route sculpt and cell materials through the shared spherical operations, and keep one body per undoable stroke. Verify body isolation, radial height sampling and real macOS Scene input.

Planetary sub-cell surface transfer: display explicit per-face detail heights, preserve the selectable base grid and sample the displayed layer. Verify a sub-cell peak on an otherwise flat planet.

Planetary material transfer: resolve face material overrides and inherited cell layers using the runtime terrain look. Verify painted tint, undo, redo and save/reopen in packaged Scene.

Static Geometry diagnostics: show the active Game renderer’s STRATA/LOD decisions and cooked-source counts in HTML. Verify model selection and stop clearing on macOS.

Authoring static geometry: load project scenery in the native map Scene, update routing after saved mode changes and expose renderer decisions before Play. Verify edited routing and drawn models in packaged Studio.

Static Geometry document scope: resolve world-space map geometry from the map, show and edit that file in the current document, and reject stale map/file writes. Keep project-scoped operations compatible.

Hosted actor geometry: report Game routing from the initialized actor renderer only for the open source document. Verify map switches, return to its live Game report and Stop.

Assets project browsing: keep the indexed content catalog as the entry point, add read-only navigation at project root, folder search and full path copy. Verify nested maps, root/up navigation and returning to content in packaged Studio.

Finish selectable file rows, keyboard copy and folder context actions in the HTML project browser.

GPU asset authoring: expose project-scoped list/read/validate/save of fx/gpu emitters and event routes through canonical SpectraFX codecs. Preserve raw source, reject stale saves and edit in the HTML GPU functions window.

GPU files: expose emitter execution, capacity, alpha and fallback policies and route quality/budget as editable fields.

GPU emitter playback: give the authoring panel its own native surface and hybrid runtime instance. Verify draft execution mode changes, pause, step, restart and camera with Scene closed.

Detached emitter: the visible native preview window reported hidden in its embedded WKWebView during desktop capture. Keep placement in CSS coordinates and let the native host decide when its window is drawable; verify actual automatic placement, modal occlusion, and docked/detached capture.

SpectraFX graph migration: open v1 assets, explicitly upgrade in memory, discard or save drafts, guard both draft and published replacements. Verify conflict isolation and retained graph topology.

SpectraFX local edit history: share DraftHistory across node edits, moves, curves and parameter changes. Commit one edit per drag and keep the original disk revision independent from undo/redo.

SpectraFX canvas connections: port the legacy output/input selection with native execution and value validation. Verify wiring, undo, redo and persisted destination ports.

SpectraFX inspector: expose the canonical module field schema, edit and remove individual properties without flattening complex values, and edit node properties, graph parameters and local function definitions with undo and guarded save.

SpectraFX function libraries: import a reusable package into the current draft and export local definitions with a checked disk revision. Keep selected properties visible beside the canvas when space allows.

SpectraFX value nodes: edit numeric, vector, boolean, color and parameter literals in the selected-node inspector while preserving connected inputs.

Modeling to Attachments: route the active model editor to the shared attachment panel and retain its unsaved graph while switching tabs.

Models: render the original project model directly on a dedicated native surface using the existing imported-model provider; verify independent orbit and unchanged assets with Scene closed.

Models: inspect the authored game type through the shared GameView with owner color, idle animation and panel-local camera; preserve source-only inspection.

Model Animation: choose an imported model and clip, sample its native pose, play, pause and scrub on an independent panel surface.

Animation authoring navigation: implement animation_editor in the HTML host, reveal project-scoped clips or the embedded model graph, preserve edited drafts and verify linked requests.

Dock panels: add Maximize and Restore to the shared ellipsis menu; keep split ratios, mounted panel state and native preview placement across both actions.

The dock menu supports Maximize and Restore throughout HTML Studio. A native window containing just one dock area uses system window zoom. Packaged macOS verification and screenshots are recorded in docs/evidence/studio-panel-maximize-native.json and docs/evidence/studio-project-catalog/panel-maximized.png.

Linked graph parity also requires a graph request made during initial model-list loading to complete once that load settles.

Packaged macOS Studio verified external animation_editor requests at startup for clips and embedded pose graphs, default model, invalid names/tabs and queued graph selection after discarding an edited draft. The source documents and revisions remained unchanged. Evidence: docs/evidence/studio-linked-animation-graph.json.

Standalone Animation Graph and Rig parity audit: both legacy panels expose undo and redo. HTML currently loses redo and draft history when switching dock tabs. Restore both via the existing graph draft-history owner and verify graph and rig histories stay independent.

macOS Graph and Rig now have verified independent Undo/Redo across dock tab switches, including when Undo returned to the saved document. Discard clears both directions. Evidence at docs/evidence/studio-animation-graph-rig-history.json.

Standalone animation editor status: expose the graph or rig panel's authored document and dirty state to a linked client together with the matching native preview's evaluated local bone translations, contacts and locomotion report, without advancing playback.

Packaged Mac Studio and its external MCP sidecar verified animation_editor_status and rig_editor_status for independent live previews, editor drafts, preview errors, contact fields and project changes. Evidence: docs/evidence/studio-linked-editor-status-discovery.json. Standalone editor open and per-entity runtime status remain separate gaps.

Standalone graph opening: port animation_editor_open and rig_editor_open for saved assets and inline documents; preserve dirty refusal, revision checks and independent panel routing.

Standalone animation_editor_open and rig_editor_open passed packaged macOS linked acceptance for named and inline documents, default untitled, explicit save, dirty refusal and conflicting on-disk revisions.

Play-test animation diagnostics: port the legacy runtime status queries onto the active hosted client's entity animation store. Show matching entity/contact/locomotion rows from both standalone HTML animation panels and verify separation from their asset previews.

Graph and Rig now read the running hosted game's procedural animation diagnostics through animation_runtime_status and rig_runtime_status. A packaged Mac run verified two different entities, the active attached climbing stance, and empty data after stopping. Evidence: docs/evidence/studio-live-entity-animation-owners.json.

Graph and Rig authoring: restore rule-type model selection, explicit save and apply to the selected type, and direct Attachments navigation. Preserve graph revision checks and authored type overrides for inherited models.

Packaged Mac Studio verified applying an animation to a child type with an inherited model, rejecting a missing model before asset save and rejecting an externally changed graph before apply. Authored child settings, base type and sibling type were checked on disk. Attachments opens from the Rig header.

Standalone Graph/Rig subgraphs: create named children in the draft, open child scopes, connect a subgraph node to a child and expose its parameter bindings. Keep graph history and reject recursive links before applying them.

Packaged Mac HTML Studio verified creating and undoing a named child, restoring it with Redo, binding its scalar input to a parent control, saving, reopening, validating and stepping the nested preview. Duplicate creation and indirect recursive linking were rejected. Evidence: docs/evidence/studio-animation-subgraph-authoring.json.

Port the legacy recursive node-property editor and typed graph parameter defaults into standalone Graph and Rig authoring. Keep node identity, connections and subgraph selection under their existing editors and verify editing beyond the original short list of HTML fields.

Standalone animation node inspector: packaged macOS verification covered editing and adding nested clip notify events, speed and loop, retaining unrelated node metadata, removing fields with Undo/Redo, vector and boolean graph defaults, native validation and reopening the saved asset.

Standalone pose canvas: reuse the shared Studio GraphCanvas for zoom, fit, panning, minimap and direct port wiring. Keep native connection validation, grouped movement history and existing signed document coordinates.

Packaged macOS standalone animation canvas verified against a saved graph with signed coordinates, direct port reassignment, movement history and navigation. Native physical pointer capture remains to verify.

Fix Escape during a captured port drag. Verify with trusted browser pointer input and grouped selection.

Port state rename and removal to the existing scoped animation document editor. Preserve transition metadata and allow returning to a stateless pose graph. Verify native save, compile and draft undo.

Standalone States inspector: rename and remove verified in the packaged Mac host with nested scope, incoming and self transitions, history, saved data and a return to the default pose graph.

Redesign the States pane around state selection, state-specific transition editing and graph-wide controls. Check narrow and wide panel layouts, including authoring a self transition with one state.

States layout and one-state transition authoring verified in docked and maximized macOS Studio. Screenshots and the native validation receipt accompany the feature manifest.

Port flat Scene world axes, object bounds and separate selection-box visibility from the legacy diagnostics menu. Use the existing document proxy data and Scene tool preferences. Compare actual native frames with layers disabled and enabled.

Port live flat-map PHYSIS wind and fire direction overlays. Share the legacy field sampling rules and read completed game ECS frames only for the matching authored map. Verify the rendered Scene and clearing on stop.

The Scene overlays menu shows PHYSIS wind and fire direction from a running local flat-map play-test. The native Mac comparison used a paused game with 64 PHYSIS cells and an authored wind field and ignition. The source map remained byte-identical after stop. A standalone edit-mode nature preview and Windows visual acceptance remain open.

Restore adjustable nature-field radius (8–200 m) and spacing (2–32 m) in the Scene overlays menu. Verify actual frame differences, preference persistence and input bounds.

Field sampling controls on the Scene menu were verified at a frozen game tick in packaged macOS Studio. Radius and spacing changed the rendered arrows and survived a full app relaunch.

PHYSIS Diagnostics: migrate the private map simulation, single and 60-tick stepping, cell ignition and reset into the panel. Verify with Scene and Game closed.

7f5ebdbfd6a7261faf70b42de01088298f643852 added StudioNaturePreview.h and the Current map preview source in PhysisDiagnosticsPanel.tsx. Packaged macOS Studio with only PHYSIS open verified authored wind contours, field values after cell ignition, ticks 0/1/61, stale document rejection, deterministic reset, independent demo state and unchanged map bytes. Receipt docs/evidence/studio-private-map-nature-preview.json and screenshot docs/evidence/studio-project-catalog/private-map-nature.png. TypeScript, Vite, 299 web tests and physis_nature_preview passed. Next migrate the private nature playback and wind settings; grass/FX/audio presentation, old-editor retirement, Windows and merged-tree acceptance remain open.

Migrate private prevailing-wind speed and direction. Verify field measurements after advancing, bounds, reset and map isolation.

f199ef76f4cdf85146c274163a7aff8ff7eaa2aa: StudioNaturePreview.h and PhysisPreviewWind.tsx transfer the speed and direction controls. Real macOS HTML5 Studio measured tick-60 cell wind [15.926,1.596,-0.512] before steering and [8.266,1.596,17.788] after setting 21.5 m/s towards 90°. Tested invalid inputs, retained settings after remount and reset, unchanged map bytes and closed Scene. Receipt docs/evidence/studio-private-nature-wind.json; screenshot docs/evidence/studio-project-catalog/private-nature-wind.png. TypeScript, Vite, native Studio build and 299 web checks passed. Next transfer continuous preview playback with pause and speed scaling. Grass/FX/audio and full retirement gates remain outstanding.

Transfer private PHYSIS playback using its existing fixed-tick scheduler. Exercise play, speed, pause, hidden panels and stepping.

8ce956a83118052653cb114748aab9fa8ae7370c verified playback in packaged macOS Studio. The PHYSIS diagnostics panel and native StudioNaturePreview use the same PhysisNaturePreview elapsed-time scheduler as the old editor. With only PHYSIS visible, 1.3 s intervals at 1× and 6× advanced 9 and 58 ticks, respectively. Verified pause, manual step, reset, source switching, hidden dock, invalid parameters and unchanged authored map bytes. TypeScript, Vite, 299 web checks and focused physis_nature_preview CTest passed including fractional time and bounded catch-up. Receipt docs/evidence/studio-private-physis-playback.json; screenshot docs/evidence/studio-project-catalog/private-physis-playback.png. Next restore the authored grid and humidity settings in this panel. Independent 3D grass, flames/smoke and audio require the dedicated native viewport; its draw loop must not double-step the existing private world. Windows, full parity and merged-tree gate remain pending.

Transfer PHYSIS preview humidity and cell-size selection, including the authored map grid and climate. Verify real grid dimensions, initial values, reset and map isolation.

bcef0f4d6ebec69fd2c2a8895938d5aa382de0d0 verified in packaged macOS Studio. PhysisPreviewGrid.tsx and StudioNaturePreview.h report actual native dimensions and climate. The test changed the private 12×8 grid at 2 m / 75% humidity to the authored 8×8 grid at 10 m / 50%; measured humidity in the same cell changed from 0.848145 to 0.598145. Playback stopped at rebuild, bad settings preserved the tick, reset retained the selected grid. Separate map with authored PHYSIS disabled exposed only the custom grid. Both maps stayed byte-identical. TypeScript, Vite, 299 web checks and physis_nature_preview CTest passed. Receipt docs/evidence/studio-private-physis-grid.json and screenshot docs/evidence/studio-project-catalog/private-physis-grid.png. Next implement independent native 3D grass/fire/smoke presentation using PhysisNaturePreview::draw and its existing private World, with a dedicated Studio surface and a single simulation clock. Audio, Windows and final legacy retirement gates remain open.

Bind a separate Studio surface to the existing private PHYSIS world. Exercise grass, fire, smoke, sound controls, reset and hidden surface cleanup with Scene closed.

444cd87c81ee707621b0f572149612530b8bb46f verified the dedicated PHYSIS Metal surface implemented in 6a4b4e561a59e6064a126c9bba35685a1eb1f793. StudioScene::drawNature presents the private ECS read-only using the existing grass, particle and audio adapters. No Scene or Game dock was present. Real captures changed with layers; tick stayed at 1 during rendering, then reset to 0. Grass, fire/smoke and audio toggles zeroed the respective native counters. Both flipbooks and their license matched the engine assets in the copied app. TypeScript, Vite, 299 web tests and physis_nature_preview CTest passed. Receipt docs/evidence/studio-physis-textured-nature.json and screenshot docs/evidence/studio-project-catalog/physis-native-nature.png. Next transfer orbit/zoom and direct ignition at the pointer to the independent Nature viewport; currently select a cell on the field map for ignition and camera framing is fixed. Grass quality and sound volume, detached-window verification, Windows and final legacy retirement remain pending.

Transfer orbit, zoom and opt-in click ignition to the independent nature viewport. Use the rendered terrain mesh and camera for ray picking and verify native input.

9326da8ec2c139869da4b5226eb03f7106e8608c verified with native AppKit input to StudioFxView over a sloped 24×16 terrain patch. Drag rotated yaw/pitch .60/.55 to .35/.65 with ignition armed and no fire, wheel zoom changed distance 21.6 to 17.496. A click at the projected point (14,5.5,10) ignited the expected cell (3,2) while tick stayed zero. Inactive ignition and map bytes verified. TypeScript, Vite and 299 web checks passed. Receipt docs/evidence/studio-physis-nature-pointer.json and screenshot docs/evidence/studio-project-catalog/physis-terrain-pointer.png. Next publish the picked coordinates to the field-map inspector, including paused native changes. Windows runtime gate and remaining legacy migration are open.

b7406df0366e348d38d0a4748579061043c6816b verified paused native ignition updates the HTML field and selected-cell inspector to Fire 1.0000 at (3,2), including after switching from the 3D surface to the field map. Same scoped map bytes, orbit and zoom checks passed. Receipt docs/evidence/studio-physis-nature-picked-cell.json, captures docs/evidence/studio-project-catalog/physis-picked-{3d,field}.png. Production Mac, TypeScript, Vite and 299 web checks passed. Next exercise a pick outside the currently sampled area and keep the inspector on that cell after recentering. Grass quality, sound volume, detached window and Windows runtime coverage remain open.

76d80d3a7eb1aaff0ad0afb570082ee88324ed6e verified in packaged macOS Studio. PhysisDiagnosticsPanel.tsx requested the exact cell and moved its field map from (900,900) to (3,2) after native terrain ignition. The inspector showed Fire 1.0000 on tick zero and the white selection persisted when returning to the field map. Scene stayed closed and the map file stayed byte-identical. Evidence docs/evidence/studio-physis-pointer-recenter.json. TypeScript, Vite, native build, 299 web checks passed. Next transfer native preview grass quality and master volume, then verify the detached PHYSIS window. Legacy retirement and Windows checks are still pending.

Transfer nature-preview grass quality and sound volume, then verify the settings reach the renderer and audio engine in a detached HTML panel.

Legacy nature MCP reconciliation: bring the full 1-64 m detail grid and adjustable 0-40 m bare shore band to the private Studio simulation before wiring legacy automation to it.

c0827893e195d639fed52ca32bb07bb3116741ed verified in the detached macOS HTML panel. A 64 m cell worked. On the same 2 m grid with authored NEREID water, ignition at (7,4) failed with a 40 m shore and succeeded at 0.5 m. Invalid widths and sizes preserved the running tick and settings, reset retained the selected width, and the map bytes matched. Receipt docs/evidence/studio-physis-shore-detached.json and capture docs/evidence/studio-project-catalog/physis-bare-shore.png. Next move the MCP operation to Studio with shared preview state, playback and read-only live-source handling.

Register the original nature command in the linked MCP registry and operate on the same transient state as the HTML panel. Verify world-space ignition, batched steps and map isolation.

f43d8fb393c85b1c9a45a01cd8739a1b40919bfb verified with packaged aiversemcp and detached macOS Studio. Discovery and linked calls set the private grid, wind and shore, ignited by world coordinates and advanced 13 ticks. The HTML panel saw tick 13, stepped to 14 and MCP read 14. Invalid ignition preserved tick, playback advanced, disable blocked stepping and re-enable reset. Map bytes matched. TypeScript, Vite, 299 web checks, native build and focused CTest passed. Receipt docs/evidence/studio-physis-linked-nature.json. Next verify running-game read-only routing and matching live presentation counters, opening the panel from MCP and capturing its surface. Windows and legacy retirement are pending.

Route explicit MCP enable to the owning PHYSIS window, including a closed dock, and select the active source. Validate that status reads do not open a panel.

75f83a3817cc07e80f3d5b3e08fcaf9328fe877a verified with packaged macOS Studio and linked MCP. A read preserved the console-only dock. Explicit enable opened PHYSIS, selected Current map preview and rendered tick 13. After detaching using the dock menu and switching to Map zones, enable activated the detached viewport at tick 19. No duplicate in main, map bytes identical. TypeScript, Vite, native build and 299 web checks passed. Receipt docs/evidence/studio-physis-open-panel.json and capture docs/evidence/studio-project-catalog/physis-open-panel.png. Next wire PHYSIS GPU surface readback to view_screenshot, classify transient commands in agent history and complete read-only live preview.

Capture the PHYSIS panel GPU surface through linked view_screenshot, including detached panels and closed Scene. Preserve existing Scene capture and keep nature commands transient in agent history.

5f48a6e16ec04419a2336aefd0a1c85c01feaba2 verified GPU PNG via linked MCP with Scene closed; hiding fire and grass at tick 19 changed 128 of 1860 samples. Compact float clipped the 370x221 CSS surface to just 370x43 pixels. Receipt docs/evidence/studio-physis-surface-capture.json. Bring the requested 3D viewport fully into view before consuming the MCP open request and verify actual pixel dimensions.

5cc5ac2531bfd2333c3d2b5c79216bb8f35be374 verified in packaged Mac Studio. At paused tick 19 linked view_screenshot {window:"physis"} returned the full 370x222 GPU PNG from the detached 3D surface, with Scene closed. Disabling grass, fire and sound changed 3290 of 9176 sampled pixels. Hidden/disabled panels and Scene-specific camera overrides returned errors; capture preserved the tick and map bytes. TypeScript, Vite, 299 web checks, studio_session, studio_owner_history and physis_nature_preview passed. Evidence docs/evidence/studio-physis-full-viewport-capture.json, docs/evidence/studio-project-catalog/physis-mcp-frame.png and physis-compact-viewport.png. Next connect read-only local game PHYSIS to this independent renderer and verify source switching.

Render the current local play-test PHYSIS world read-only in the dedicated nature viewport, verify matching paused and resumed ticks, source isolation and stop.

8b7bc5da7cf6006597f2a88f35e028860bb0d975 verified with local Game and PHYSIS open in packaged Mac Studio. StudioGameSurface::localPhysisWorld supplies a const world only for locally controlled PHYSIS. The renderer requires the matching saved game map. Rendered ticks matched paused game tick 2 and resumed tick 26, four grass patches were reported, the separate private map stayed at tick 1, and stopping the game removed the drawable. Linked MCP opened the live source, refused ignition and stepping, and returned a 504x405 live PNG. Map bytes matched. TypeScript, Vite, native build, 299 web checks and 3 focused CTests passed. Receipt docs/evidence/studio-physis-live-nature.json. Captures docs/evidence/studio-project-catalog/physis-play-test-3d.png and physis-play-test-frame.png. Next reconcile runtime field edits with the legacy PHYSIS window, including live disable and source transitions. Windows and merged-tree gates remain open.

Transfer the local opt-in FieldEditHost command from the legacy PHYSIS window to Map zones using the existing revision and scheduled-tick confirmation. Test the opt-in module and the packaged built-in module refusal separately.

693d647b411fb12cd7e02e1ad186c7a444c5b104 transferred the legacy local field editor to Map zones in HTML Studio. StudioPhysisFieldEdit reuses FieldEditSession. Native physis_live_edit verified explicit module consent, stale runtime/map/field revision rejection, duplicate protection, queued status and confirmed wind speed 22 after the scheduled tick. Packaged Mac Studio displayed the built-in module refusal, rejected apply, stale scope and unknown action, and disabled the control after Stop. Map bytes matched. Receipt docs/evidence/studio-physis-local-field-edit.json, screenshot docs/evidence/studio-project-catalog/physis-local-field-edit.png. TypeScript, Vite, 299 web checks, Mac native build and 2 focused CTests passed. Positive application is verified with the opt-in GameModule fixture; no built-in module implements FieldEditHost. Next migrate authenticated project-supplied remote field transport, preserving server authority, revision and retry semantics. Windows and merged-tree gates remain pending.

Transfer the authenticated project-host remote field editor to Map zones and verify revision discovery, guarded submit, retry and authoritative receipts.

b7f624c341654142b25164f98228a3cddce53aee wired the authenticated EditorLink into HTML Studio Map zones. Server reconciliation is polled by the native host, including while the panel is closed. The loopback test drove the Studio adapter with an attached authenticated route, rejected stale map/server revisions and connection context, retained original request bytes and ID on explicit retry and confirmed wind 24 at server revision 1. Packaged Mac verified offline UI, inactive submission and revision controls, stale document rejection, unchanged map bytes. Receipt docs/evidence/studio-physis-remote-field-edit.json, screenshot docs/evidence/studio-project-catalog/physis-remote-fields.png. TypeScript, Vite, 299 web checks, native build, physis_remote_edit and physis_live_edit passed. Positive remote operation was exercised by the native integration fixture; the packaged project supplied no server connection. Next check live nature layer controls, enable/disable transitions and the remaining Scene overlay controls against PhysisWindow.cpp. Windows and merged-tree gates remain pending.

Transfer grass, fire/smoke and audio layer controls, quality and gain to the local game PHYSIS viewport. Verify that display-only edits preserve paused ECS and private preview settings.

80cebef08ba2a1b99111261abc4ba900088b6091 verified live grass, fire/smoke and audio toggles at paused game tick 2 in packaged Mac Studio. All three renderer counters reached zero; grass High set cap 4608, gain 25% reached the audio engine. Invalid quality, ignition and stale runtime rejected. Private map remained at tick 1, medium and 80%. Resume matched tick 26; stop removed drawable. TypeScript, Vite, 299 web checks, 2 focused CTests and native build passed. Evidence docs/evidence/studio-physis-live-display.json and docs/evidence/studio-project-catalog/physis-live-display.png. Next separate MCP live disable/enable from the private world and verify restoration, then transfer the diagnostic overlay.

Separate the MCP live viewport lifecycle from the private preview world; verify disable, enable, hidden capture and preserved presentation settings and ticks across both sources.

Packaged macOS PHYSIS preview lifecycle verified on both world sources in [the live-cycle receipt](evidence/studio-physis-live-cycle.json). The legacy camera-centered diagnostic tiles and field outlines still require transfer to the dedicated viewport.

Share the legacy PHYSIS camera-centered diagnostic sampler with HTML Studio and draw the selected field in the dedicated nature viewport.

Transfer PRISMA view_postfx to HTML Studio, including runtime quality for the hosted Game and Scene-only visibility, without changing project files.

Route MCP Game screenshots to the active Game drawable and preserve device selection, source lifetime and hidden/stopped errors.

Packaged macOS Game capture now uses the Game drawable through linked MCP. With Scene closed, free size returned 1280 × 510 and a device-only request for iPhone SE returned 957 × 538. Idle, hidden and stopped Game captures reported unavailable. [Game screenshot evidence](evidence/studio-game-viewport-screenshot.json).

Restore whole-window MCP capture in HTML Studio, including simultaneous browser controls and native viewports, with Scene closed.

An explicit all or default view_screenshot now captures the main HTML Studio window. Packaged macOS included WKWebView controls and a live Metal Game layer. Switching to Console replaced the game region while retaining the Post FX panel. [Whole-window verification](evidence/studio-full-window-screenshot.json).

Transfer clean Scene screenshot dimensions and camera arguments through a shared request parser before wiring the isolated render target.

Route Scene exports through the current map render pass with a separate color/depth output and a temporary camera projection. Verify the resulting PNG on Metal.

Packaged macOS HTML Studio exported planar and planetary Scene PNGs at 511 × 511, 1536 × 864, the current viewport and 4096 × 2048. A per-shot orbit produced a different image on both maps. The live camera parameters, game manifest and map bytes matched after each sequence. [Scene export evidence](evidence/studio-clean-scene-export.json).

Scope Scene capture to the map accepted by the agent link and its matching render snapshot. Transfer linked view_focus for world coordinates and current planetary face cells.

Packaged macOS confirmed linked view_focus on the flat map and the planetary face under the viewport center. A same-poll reopen or alternate-map switch canceled the preceding screenshot. A fresh request then exported at 513 × 389. [Scoped capture and camera verification](evidence/studio-scene-scoped-focus-verified.json).

Transfer normalized read-only view_pick to the HTML Scene mouse-picking path. Verify both map topologies, entity identity, misses, hidden Scene, stale frames and unchanged selection.

Packaged macOS Studio verified linked view_pick against the HTML Scene selection path on planar and planetary maps. Mouse selection and a normalized off-center query identified the same authored entity, while the query itself preserved selection and history. [Desktop picking verification](evidence/studio-linked-view-pick-desktop.json).

Transfer the batched view_ground query with transformed terrain tile meshes and heightfield fallback.

Packaged macOS HTML Studio verified linked view_ground on overlapping sloped terrain tiles, partial mesh coverage, a triangle-interpolated heightfield and live tile visibility changes. [Ground height verification](evidence/studio-linked-ground-height.json).

Transfer NEREID river direction and bank gizmos to the Scene overlays menu, for the selected river or all rivers.

Packaged macOS Studio verified the selected-river and all-rivers controls, direction and bank rendering, hidden entity filtering, and unchanged map files. [River overlay evidence](evidence/studio-river-flow-desktop.json).

Transfer TERRA downhill arrows at the selected terrain tile or camera target. Use the same displayed ground sampler as linked view_ground. Verify both modes, field spacing and clean output.

Packaged macOS Studio verified terrain slope arrows on selected imported tile meshes and around the camera target. Radius and spacing are controlled in Scene overlays. Clean Scene captures omit these arrows. [Desktop and geometry checks](evidence/studio-terrain-slope-verified.json).

Transfer the selected TerraWeave tree crown wind marker, including a calm ring, renderer wind direction, terrain-relative placement and the Scene overlay toggle. Verify against the packaged HTML toolbar and Wind state.

Verified selected-tree crown wind in packaged macOS Studio with a reversed heading, disabled wind and cleared selection. [Desktop and geometry evidence](evidence/studio-tree-crown-wind.json).

Transfer the terrain-following erosion zone outlines, coefficient colours and labels to the Scene overlays menu. Verify circles and polygons on sloping ground.

Packaged macOS Studio verified circular and polygonal erosion zone outlines, coefficient and soil labels, layer visibility and clean capture. [Desktop and geometry evidence](evidence/studio-erosion-zone-overlays.json). Individual zone highlighting and non-ASCII labels remain open.

Expose map-scoped erosion zone highlighting in the Scene overlay menu. Verify none, one, all and stale document rejection without changing map history.

Packaged macOS Studio verified highlighting one or all authored erosion zones and clearing the highlight on map switch. [Native highlight evidence](evidence/studio-erosion-highlight.json). Unicode native caption rendering remains open.

Render UTF-8 erosion zone captions using the packaged engine UI font. Verify distinct Cyrillic names in exported Scene images and preserve clean exports.

Packaged macOS Studio rendered the Russian zone names Скала and Берег with the bundled UI font. Switching to a map with different equal-length Cyrillic names changed both captions, with clean exports retaining zero zone-coloured changes. [Cyrillic caption verification](evidence/studio-erosion-cyrillic-captions.json).

Route linked view_gizmos to the same Scene layer state as the HTML toolbar. Verify entity selection, all flags, distance limits, invalid request atomicity and changes from both entry points.

### Wind showcase parity

The legacy view_wind operation combines independent PHYSIS nature settings, TerraWeaveForestPreview draw submission and wind-mask debugging. HTML Studio already uses PhysisNaturePreview in StudioNaturePreview and StudioScene::NatureView. Connect the private preview to those existing owners and evaluate the TerraWeaveForestPreview draw path separately from the authored planetary forest. The acceptance map must exercise forestInstances and grassPatches, all three treeGeometry modes, horizon distance, sound, pause and all five debug views. Capture the preview with Scene closed and verify that map bytes and history survive the changes.

Frame the private forest above the canopy on first enable and preserve subsequent orbit and zoom. Compare the resulting macOS preview with the initial capture.
Feed the private viewport renderer from the sampled PHYSIS wind after the nature frame is prepared. Verify forest motion with fixed camera and other animated layers disabled.

Expose the Parts, Depth and Weight GPU forest masks in PHYSIS. Skeleton line rendering requires separate geometry.

Render the forest skeleton from the encoded wind rig and actual per-instance draw transform. Verify the fifth dropdown mode in the independent PHYSIS viewport.
Expose the TerraWeave forest LOD scale and horizon in the private PHYSIS viewport. Verify controls, projection range and terrain-map persistence on macOS.

Switch private forest tree geometry and hierarchical wind masks using the existing TerraWeave prototypes. Preserve simulation and camera during GPU rebuild.

Route the original view_wind MCP schema to the PHYSIS viewport and validate the legacy arguments before updating presentation state.

Expose legacy play_status through the HTML Studio game owner. Preserve the inactive, map match and client menu result shapes.

Route linked play_stop through the same Game surface teardown as the HTML control and verify repeat stops.
Route legacy play_start arguments to the shared HTML Studio Game launch path and open the Game tab on successful launch.

Publish the live HTML dock and floating panel inventory to editor_windows using the same panel registry as the Window menu. Verify actual open-state changes.

Route editor_window panel, workspace and reset requests to HTML Studio layout owners, including detached tabs.

Restore editor_theme using shared HTML theme preferences, Window menu controls and linked MCP updates.

Expose geometry_stats from the rendered Scene and its completed GPU queries through the linked MCP registry.

Share STRATA selection, refusal, cache, upload and preparation statistics between geometry_stats and static geometry reports.

Share the game terrain water-cell layout builder with HTML Scene so sheet, connected-body and shore levels come from one implementation.

Expose Scene debug views directly in the toolbar: STRATA clusters and routes, model wind diagnostics, PHYSIS, TerraWeave and NEREID.

Port editor_select to HTML Studio starting with map entity ordinals, canonical document IDs, camera focus, Inspector and clearing selection. Record selection follows in the same migration.
