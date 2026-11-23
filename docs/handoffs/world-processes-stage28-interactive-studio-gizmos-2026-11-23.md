# AIVERSE World Processes — Stage 28: interactive Studio Scene handles

**Date:** 2026-11-23  
**Branch:** `feature/world-processes-experimental`  
**PR:** https://github.com/chromealex/AIVERSE/pull/112 (DRAFT)  
**Main:** unchanged  
**Test policy:** user explicitly requested feature work before tests.

## Implemented functionality

1. `apps/studio-native/StudioWorldProcessesEditing.h`
   - Created at commit `57e18ed3ec6b17151b1154d89b30b8062a7867c9`.
   - Read-only extraction of interactive controls for the **selected authored map entity**. Derives its stable ID, actual linked fields and target component class from existing `WorldProcessesLinkedAuthoring.h` and `WorldProcessesExtendedLinks.h`.
   - `worldprocess_edit::handles(MapDoc,selectedEntityId)` creates one drag handle for each compatible object reference, including unbound slots, and area handles for EVOLVE visual radius and GRANULAR field width/height.
   - `hit(handles,StudioScene,cursor...)` reuses real native `StudioScene::projectPoint` and bounded nearest-point hit radius; only the selected object exposes handles, so ordinary scene clicks and camera tools are preserved.
   - `nearestTarget(MapDoc,Handle,StudioScene,cursor...)` finds the nearest actual authored compatible entity, with no raw ECS index or screen-space semantic guess. The native Session does its own final validation of the target ID.
   - `extent(DocEntity,Handle,worldGroundPoint,value)` handles EVOLVE visual metres and GRANULAR dimension resizing (1..128); all input floats are checked/bounded. This stage does not represent EVOLVE gameplay climate bounds; radius remains editor-only helper metadata.

2. `apps/studio-native/StudioSession.cpp`
   - Commit `8158e028cbb44d3358d042dd9679baeebc823fa5`: `studio.worldProcessAreaSet` adds a small, strict native RPC for two editor actions only:
     - `{componentId:"EvolveRegion",key:"radiusMetres",value:0.5..500}` updates only `DocEntity.extra.worldProcessGizmo.radiusMetres`, one Undo snapshot, stable authored object only. No new climate state.
     - `{componentId:"GranularField",key:"width"|"height",value:1..128}` converts the legacy simple 1×1 grid to the existing `GranularDocument` full-grid schema when necessary and uses `granular::authoring::resize` to preserve overlapping cell material and obstacle masks. Commits through the ordinary MapDoc Undo/Redo and Save, with fail-closed dimension/schema checks.
   - Commit `281024c7e6d14add586b0121d2029a07aa2ef84e` labels these actions in the existing Studio History panel.
   - Existing `studio.setProperty` continues to be the **only** actual linked-object write path. It validates component type, stable ID and the complete authored document before committing one Undo step.

3. `apps/studio-native/StudioHost.h` and `StudioHost.cpp`
   - Commit `625387faf4ef19083a4e2b705d9e146ab5b5df5e`: stores only ephemeral scene drag scratch in `WorldProcessDrag` (no competing MapDoc save/Undo state).
   - Commit `417c6b413b70cf7a0b1cf6161bf3e803c18827ee`: integrates with the existing platform-neutral `StudioHost::sceneMouse` path used by the native macOS/Windows Studio shells. A gizmo hit is checked on left-button press after verifying the selected stable entity, before default Move/Marquee; other scene inputs retain their established handlers.
   - During pointer drag, projects real candidate entity targets into the screen or uses the real scene `groundPoint` to calculate the requested area size. It **does not** touch MapDoc while the pointer moves.
   - At mouse release, dispatches exactly one validated native `studio.setProperty` or `studio.worldProcessAreaSet`. On error, uses the existing `assets.importError` banner; on success, sends `document.changed`/`selection.changed` and rebuilds the normal scene overlay.
   - Commit `cf137dc829c4fb90bf99470d2a9561b25cb779ca`: renders white selectable handles on the selected component and a green preview endpoint/line while the drag has a compatible target, through the **existing** `StudioScene::OverlayShape` native renderer. No new WebGL canvas or ImGui.
   - Commit `6f85deafb20682f21216bbe63c0e27e8ca273770`: Escape cancels the drag and leaves MapDoc and Undo untouched.

4. `apps/studio-native/StudioWorldProcessesGizmos.h`
   - Commit `af7c7bcd9b429198d38f40e5c71e583bd6d2cfa4`: fixes EVOLVE ring rendering to **world-space segments** (24 sides) rather than `StudioScene::OverlayShape`'s standard pixel-radius circle. The visual 3D radius now matches the handle's world-space editable radius when zooming the Scene.

## Architecture and contracts

- Render/control path: `StudioHost::sceneMouse` → `StudioWorldProcessesEditing.h` hit → native Scene project/ground query → ephemeral preview `StudioHost::rebuildOverlay` → **one** `StudioSession::dispatch` mutation at release → regular MapDoc snapshot and Undo.
- Stable references are authored map document IDs. Target compatibility is checked by both the picker and authoritative native setProperty validation. No full-generation ECS Entity is written to the map.
- GRANULAR area width/height are real grid authoring dimensions and preserve previous cell content through `GranularDocument::resize`. EVOLVE radius is **editor preview only**; the actual ecological polygon/rectangle mapping still belongs to the native `EvolvePlantRegion` host.
- Unbound links expose small socket handles near the selected owner so users can connect them without first entering a target in Inspector.
- Locked/hidden selected authored objects cannot be dragged. Camera orbit with Alt, marquee, existing transform tools, terrain strokes and spline editing retain their normal paths.

## Tests actually run

**NONE.** Per user's instruction, Stage28 code was committed but no CMake/Clang/GCC build, sanitizer, Studio native macOS/Windows Scene interaction, TypeScript browser test, screenshot, Undo/Redo runtime check or full Mac quick suite was executed. Historical Stage05 CI green predates this stage and does NOT prove its implementation.

## Known limits

- The UI requires an already selected authored entity with a stable `entity:@id`; ordinary non-authored or runtime-only entities have no handles.
- Overlapping linked endpoints in the same projected screen location can be ambiguous (nearest handle takes precedence). Multi-link hover labels and cycling are not implemented yet.
- Link drag snaps to a compatible object under the pointer; it does not create a new object or infer asset content refs.
- EVOLVE native `EvolvePlantRegion` gameplay bounds are not automatically updated by the visual gizmo radius. Later native project authoring must explicitly make these consistent.
- The new RPC is currently scoped to StudioSession; other editor/MCP map operations may require the same centralized reference-integrity rules.
- Full native QA is mandatory before calling any of this production-ready or merging PR #112.

## Next specific step

Finish live gizmo endpoint labeling, hover/overlap cycling, actual EVOLVE native region bounds authoring (rather than editor-only radius), and a project sample with an existing carrier entity type. Then, when user permits tests, run macOS/Windows native Scene, Undo/Redo and save/reopen acceptance plus all current CMake/CTest suites and full merged-tree `./run-tests.sh --quick` before merge.
