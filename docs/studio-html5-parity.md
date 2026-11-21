# HTML5 Studio full functional migration

Owner scope confirmed 2026-11-20: complete full functional transfer before main merge. Do not equate a registered tab, JSON endpoint, placeholder, disabled control or source scan with feature parity. Legacy ImGui stays available until the acceptance matrix is complete.

Baseline: main 076139d12d9ea2380a5445f823323f4e2c096811 integrated into feature/studio-html5; tested HEAD 6e54c2e5ad743caa88daa95e4355daf313f942e4. Native app builds, 17 selected CTests pass, Python contracts 19 pass, frontend build/domain tests pass. These checks do not demonstrate functional parity. Mac UI was initially blocked by locked desktop; Stage 80 later exercised native Game interactively.

## Window inventory

Derived from actual AIVERSE_EDITOR_WINDOW registrations in apps/editor/windows. Forwarding files without registrations are excluded. Each source remains the detailed behavior reference, including context menus, shortcuts and native-only workflows.

| Legacy ID | Reference | HTML5 status |
| --- | --- | --- |
| `agent` | `apps/editor/windows/AgentWindow.cpp` | Missing functional HTML5 implementation |
| `animation_assets` | `apps/editor/windows/AnimationAssetsWindow.cpp` | Missing functional HTML5 implementation |
| `animation_graph` | `apps/editor/windows/AnimationGraphWindow.cpp` | Missing functional HTML5 implementation |
| `animation_rig` | `apps/editor/windows/AnimationRigWindow.cpp` | Missing functional HTML5 implementation |
| `assets` | `apps/editor/windows/AssetsWindow.cpp` | Partial: GUID catalog/import/moves/conflict handling/import settings merged and native-tested (78–79); visual and full legacy parity pending |
| `animation_attachments` | `apps/editor/windows/AttachmentAuthoringWindow.cpp` | Missing functional HTML5 implementation |
| `branding` | `apps/editor/windows/BrandingWindow.cpp` | Missing functional HTML5 implementation |
| `build` | `apps/editor/windows/BuildWindow.cpp` | Missing functional HTML5 implementation |
| `console` | `apps/editor/windows/ConsoleWindow.cpp` | Partial; native visual parity pending |
| `fields` | `apps/editor/windows/FieldsWindow.cpp` | Missing functional HTML5 implementation |
| `fog` | `apps/editor/windows/FogWindow.cpp` | Missing functional HTML5 implementation |
| `FpsWindow` | `apps/editor/windows/FpsWindow.cpp` | Missing functional HTML5 implementation |
| `effects` | `apps/editor/windows/FxWindow.cpp` | Missing functional HTML5 implementation |
| `game` | `apps/editor/windows/GameWindow.cpp` | Partial: native shared GameClient Play map/menu/Stop, Metal surface, keyboard/mouse, runtime menu pause/resume visually verified (80); editor pause/step/restart verified (81); device dimensions/orientation/safe area/touch verified (82); runtime inspection, FPS and device chrome still pending |
| `generate` | `apps/editor/windows/GenWindow.cpp` | Missing functional HTML5 implementation |
| `hierarchy` | `apps/editor/windows/HierarchyWindow.cpp` | Partial: map entity create/duplicate/delete, single selection/search, undo/save/reopen tested (Stage 69); grouping/multiselect/FPS authoring and visual QA pending |
| `project_import` | `apps/editor/windows/ImportProjectWindow.cpp` | Missing functional HTML5 implementation |
| `input` | `apps/editor/windows/InputWindow.cpp` | Missing functional HTML5 implementation |
| `inspector` | `apps/editor/windows/InspectorWindow.cpp` | Partial; native visual parity pending |
| `lightbake` | `apps/editor/windows/LightBakeWindow.cpp` | Missing functional HTML5 implementation |
| `lighting` | `apps/editor/windows/LightingWindow.cpp` | Partial; native visual parity pending |
| `materials` | `apps/editor/windows/MaterialsWindow.cpp` | Partial; native visual parity pending |
| `AnimationWindow` | `apps/editor/windows/ModelAnimationWindow.cpp` | Missing functional HTML5 implementation |
| `models` | `apps/editor/windows/ModelWindow.cpp` | Missing functional HTML5 implementation |
| `ModelingWindow` | `apps/editor/windows/ModelingWindow.cpp` | Missing functional HTML5 implementation |
| `overlays` | `apps/editor/windows/OverlaysWindow.cpp` | Missing functional HTML5 implementation |
| `physis` | `apps/editor/windows/PhysisWindow.cpp` | Missing functional HTML5 implementation |
| `postfx` | `apps/editor/windows/PrismaWindow.cpp` | Missing functional HTML5 implementation |
| `studio_asset_audit` | `apps/editor/windows/ProjectAuditWindow.cpp` | Missing functional HTML5 implementation |
| `project` | `apps/editor/windows/ProjectWindow.cpp` | Partial: native map new/open/save/save-as and dirty close/quit protection (Stage 71); project creation/settings/import/FPS templates and visual QA pending |
| `raid` | `apps/editor/windows/RaidWindow.cpp` | Missing functional HTML5 implementation |
| `roads` | `apps/editor/windows/RoadsWindow.cpp` | Missing functional HTML5 implementation |
| `rules` | `apps/editor/windows/RulesWindow.cpp` | Partial: unit-type form (health, move, vision, cost, weapons, flags) + raw JSON per type via rules_get/rules_set (stage 100); techs/resources/triggers/damage table pending |
| `scene` | `apps/editor/windows/SceneWindow.cpp` | Partial: native rigid-model render/picking/hot reload merged, cache release corrected (78–79); Unity-style navigation + orientation gizmo with axis snap and Persp/Iso (91, web/Linux-checked, Mac build pending); transform gizmos, rect selection, broader authoring/model formats and visual parity pending |
| `scripts` | `apps/editor/windows/ScriptsWindow.cpp` | Missing functional HTML5 implementation |
| `shader_graph` | `apps/editor/windows/ShaderGraphWindow.cpp` | Partial; native visual parity pending |
| `spectrafx_gpu_assets` | `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` | Missing functional HTML5 implementation |
| `spectrafx_graph` | `apps/editor/windows/SpectraFxGraphWindow.cpp` | Missing functional HTML5 implementation |
| `spectrafx_graph` | `apps/editor/windows/SpectraGraphWindow.cpp` | Missing functional HTML5 implementation |
| `stats` | `apps/editor/windows/StatsWindow.cpp` | Missing functional HTML5 implementation |
| `studio_camera_bookmarks` | `apps/editor/windows/StudioCameraBookmarksWindow.cpp` | Missing functional HTML5 implementation |
| `studio_extensions` | `apps/editor/windows/StudioExtensionsWindow.cpp` | Missing functional HTML5 implementation |
| `studio_frame_capture` | `apps/editor/windows/StudioFrameCaptureWindow.cpp` | Missing functional HTML5 implementation |
| `studio_snapshot_diff` | `apps/editor/windows/StudioSnapshotDiffWindow.cpp` | Missing functional HTML5 implementation |
| `terrain` | `apps/editor/windows/TerrainWindow.cpp` | Missing functional HTML5 implementation |
| `texture_resources` | `apps/editor/windows/TextureResourcesWindow.cpp` | Missing functional HTML5 implementation |
| `tiles` | `apps/editor/windows/TilesWindow.cpp` | Missing functional HTML5 implementation |
| `tools` | `apps/editor/windows/ToolsWindow.cpp` | Missing functional HTML5 implementation |
| `water` | `apps/editor/windows/WaterWindow.cpp` | Missing functional HTML5 implementation |

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
