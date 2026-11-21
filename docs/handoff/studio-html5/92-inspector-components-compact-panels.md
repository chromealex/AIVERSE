# Stage 92 — component Inspector, Mesh Renderer, compact Hierarchy/Assets (2026-11-21)

Branch `feature/studio-html5`. User requests: Inspector must show entity
components (incl. a renderer component with mesh + materials), full Transform,
compact Hierarchy with search on top, drag, delete, context menu; compact
Assets; content over decoration.

## Native (`apps/studio-native`)
- `StudioSession::inspectorFor(id)` / `studio.inspect` → `{id,name,kind,nameEditable,components[]}`;
  also `snapshot.inspector` for the selection. Components:
  - **Transform** — authored entities: X/Y/Z, Rotation Y (`yaw`), Scale (editable,
    `components.Transform`). Legacy map units: X/Z editable, Y = terrain height
    (read-only, `terrainPreviewHeight`, same as the Scene), **Rotation Y = map
    `facing`** (editable, 0..360, the field the RTS runtime reads), Scale read-only 1
    (the RTS runtime has no per-instance scale yet). Lights: X/height/Z. FPS: X/Y/Z.
  - **Mesh Renderer** — authored entities: Mesh (`StaticModel.asset`, asset picker,
    GUID or `content/` path, .glb/.gltf/.obj, must exist) and one material per slot
    (`StaticModel.materials[slot|"*"]`, names from `materials.json`, "" = model
    default). Slot names come from the loaded model via
    `setMaterialSlotProvider` (Main.mm → `StudioMetalScene::modelMaterialSlots`);
    unknown → "*" (all slots). Legacy entities show the type's `view.model` /
    `view.materials` read-only.
  - **Unit** (type read-only, owner), **Light**, FPS Collider/Spawn and any other
    authored component (e.g. NereidWater) read-only.
- `setMeshRenderer` validates before `beginEdit` (undoable). Preview:
  `StudioMetalScene` resolves `StaticModel.materials` with the shared
  `AppearanceChoiceAuthoring`/`AppearanceAuthoringService` against
  `materials.json` into per-instance `ModelDraw::materials` (factors only; texture
  maps in library materials are not loaded by the preview yet).
- `studio.entityMove {entityId,targetId?,placement}` — Hierarchy drag reorder of
  map entities, undoable; **road endpoints (index pairs) are remapped**.
- Scene proxies/models honour legacy `facing`.
- Not a runtime claim: `StaticModel` is consumed by Studio preview/NEREID only;
  the RTS game runtime does not yet render `StaticModel` meshes/material overrides.

## Web (`apps/studio-web`)
- `panels/InspectorPanel.tsx/.css`: entity header (name editable for lights),
  kind + id, collapsible component cards (state persisted), Unity-style rows:
  Position X Y Z, Rotation, Scale; notes as an info tooltip; lock button inline.
  Pinned Inspector re-reads via `studio.inspect`.
- `panels/HierarchyPanel.tsx/.css`: 20px rows, search pinned on top (⌘F, Esc clears,
  ↓ into list), +Create (type select), ↑/↓ navigation, Delete/⌫, ⌘D, F / double-click
  focus, right-click menu (Focus, Duplicate, Delete, Create), drag reorder with
  insertion line (disabled while searching / for non-map objects).
- `ui/StudioContextMenu.tsx` shared menu (keyboard, outside-click/Esc close).
- Assets: compact density overrides, no sidebar caption, 76px grid tiles, 20px list
  rows/folders, context menu (Open/Import Settings, Copy Path, New Folder, Import, Reload).

## Checks
- tsc OK; `npm run test:web` 97/97; Python contracts 41/41 (pin contract updated).
- clang++ 18 `-fsyntax-only` on StudioSession.cpp, StudioMetalScene.cpp, session tests.
- macOS (`build/claude/studio-rebuild-run.command`, KINGSTON): vite build, `aiverse_studio`
  + `aiverse_studio_session_tests` built, **session contract PASS** (new checks:
  inspect components, Mesh Renderer validation/undo, reorder remaps roads, legacy
  full Transform + facing write/undo).
- Interactive on Mac (qa-lua-lab): compact Hierarchy, townhall Inspector shows
  Transform (X/Y/Z, Rotation, Scale), Mesh Renderer, Unit; Rotation 45 wrote
  `facing` (Unsaved/Undo), undone; not saved.
- Not exercised interactively: context menu & drag (automation cannot right-click /
  HTML drag in the background), Mesh Renderer on an authored entity (QA project has
  none), material preview pixels.

## Next
- Runtime support for per-instance scale / StaticModel in RTS view, then make
  legacy Scale editable. Add Component / Remove Component. Rename for map entities.
