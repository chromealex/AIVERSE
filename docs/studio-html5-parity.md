# HTML5 Studio full functional migration

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

## Window inventory

Derived from actual AIVERSE_EDITOR_WINDOW registrations in apps/editor/windows. Forwarding files without registrations are excluded. Each source remains the detailed behavior reference, including context menus, shortcuts and native-only workflows.

| Legacy ID | Reference | HTML5 status |
| --- | --- | --- |
| `agent` | `apps/editor/windows/AgentWindow.cpp` | Agent panel: Claude Code over the agent link, change list, Undo all, screenshots (109/110) |
| `animation_assets` | `apps/editor/windows/AnimationAssetsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `animation_graph` | `apps/editor/windows/AnimationGraphWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `animation_rig` | `apps/editor/windows/AnimationRigWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `assets` | `apps/editor/windows/AssetsWindow.cpp` | Partial: GUID catalog/import/moves/conflict handling/import settings merged and native-tested (78–79); visual and full legacy parity pending |
| `animation_attachments` | `apps/editor/windows/AttachmentAuthoringWindow.cpp` | Attachments panel: layout JSON via the engine codec; no live GPU preview (111) |
| `branding` | `apps/editor/windows/BrandingWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `build` | `apps/editor/windows/BuildWindow.cpp` | Build panel: toolchain, estimate, build, log (103) |
| `console` | `apps/editor/windows/ConsoleWindow.cpp` | Partial; native visual parity pending |
| `fields` | `apps/editor/windows/FieldsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene field brush (111) |
| `fog` | `apps/editor/windows/FogWindow.cpp` | Fog of War panel, same controls (108) |
| `FpsWindow` | `apps/editor/windows/FpsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) (fps + LENS) |
| `effects` | `apps/editor/windows/FxWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `game` | `apps/editor/windows/GameWindow.cpp` | Partial: native shared GameClient Play map/menu/Stop, Metal surface, keyboard/mouse, runtime menu pause/resume visually verified (80); editor pause/step/restart verified (81); device dimensions/orientation/safe area/touch verified (82); runtime inspection, FPS and device chrome still pending |
| `generate` | `apps/editor/windows/GenWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `hierarchy` | `apps/editor/windows/HierarchyWindow.cpp` | Partial: map entity create/duplicate/delete, single selection/search, undo/save/reopen tested (Stage 69); grouping/multiselect/FPS authoring and visual QA pending |
| `project_import` | `apps/editor/windows/ImportProjectWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) (port_*) |
| `input` | `apps/editor/windows/InputWindow.cpp` | Input panel: InputFlow actions/contexts/bindings via the engine codec (108) |
| `inspector` | `apps/editor/windows/InspectorWindow.cpp` | Partial; native visual parity pending |
| `lightbake` | `apps/editor/windows/LightBakeWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `lighting` | `apps/editor/windows/LightingWindow.cpp` | Partial; native visual parity pending |
| `materials` | `apps/editor/windows/MaterialsWindow.cpp` | Partial; native visual parity pending |
| `AnimationWindow` | `apps/editor/windows/ModelAnimationWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110); no timeline view |
| `models` | `apps/editor/windows/ModelWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `ModelingWindow` | `apps/editor/windows/ModelingWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `overlays` | `apps/editor/windows/OverlaysWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `physis` | `apps/editor/windows/PhysisWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `postfx` | `apps/editor/windows/PrismaWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `studio_asset_audit` | `apps/editor/windows/ProjectAuditWindow.cpp` | Asset Audit panel (108) |
| `project` | `apps/editor/windows/ProjectWindow.cpp` | Partial: native map new/open/save/save-as and dirty close/quit protection (Stage 71); project creation/settings/import/FPS templates and visual QA pending |
| `raid` | `apps/editor/windows/RaidWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `roads` | `apps/editor/windows/RoadsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene road drawing (111); no node gizmos yet |
| `rules` | `apps/editor/windows/RulesWindow.cpp` | Partial: unit-type form (health, move, vision, cost, weapons, flags) + raw JSON per type via rules_get/rules_set (stage 100); techs/resources/triggers/damage table pending |
| `scene` | `apps/editor/windows/SceneWindow.cpp` | Partial: native rigid-model render/picking/hot reload merged, cache release corrected (78–79); Unity-style navigation + orientation gizmo with axis snap and Persp/Iso (91, web/Linux-checked, Mac build pending); transform gizmos, rect selection, broader authoring/model formats and visual parity pending |
| `scripts` | `apps/editor/windows/ScriptsWindow.cpp` | Scripts panel: Lua editor, save with syntax check, Check (108) |
| `shader_graph` | `apps/editor/windows/ShaderGraphWindow.cpp` | Partial; native visual parity pending |
| `spectrafx_gpu_assets` | `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) |
| `spectrafx_graph` | `apps/editor/windows/SpectraFxGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; full Niagara graph editing and GPU particle preview still pending |
| `spectrafx_graph` | `apps/editor/windows/SpectraGraphWindow.cpp` | Stages 5–6 Graph v2: typed Data Interface editor, project Assets texture picker, native validation/drafts/publish, node topology canvas with drag/zoom and native add/connect/disconnect; full Niagara graph editing and GPU particle preview still pending |
| `stats` | `apps/editor/windows/StatsWindow.cpp` | Statistics panel: map contents, Scene fps (110) |
| `studio_camera_bookmarks` | `apps/editor/windows/StudioCameraBookmarksWindow.cpp` | Scene toolbar bookmarks (106) |
| `studio_extensions` | `apps/editor/windows/StudioExtensionsWindow.cpp` | Extensions panel: editor/extensions.lua widgets, commands, Scene gizmos (111) |
| `studio_frame_capture` | `apps/editor/windows/StudioFrameCaptureWindow.cpp` | Frame Capture panel: frame times, draw calls, capture (111) |
| `studio_snapshot_diff` | `apps/editor/windows/StudioSnapshotDiffWindow.cpp` | Snapshot Diff panel (110) |
| `terrain` | `apps/editor/windows/TerrainWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene brush: sculpt, paint, objects (110/111) |
| `texture_resources` | `apps/editor/windows/TextureResourcesWindow.cpp` | Texture Resources panel: live GPU textures, estimate (111) |
| `tiles` | `apps/editor/windows/TilesWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene tile brush (111) |
| `tools` | `apps/editor/windows/ToolsWindow.cpp` | Scene toolbar (Q/W/E/R, snap, pivot, bookmarks) and brushes |
| `water` | `apps/editor/windows/WaterWindow.cpp` | Op panel (Workspace ops, schema forms, History steps; stage 108/110) + Scene river drawing (111) |

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
