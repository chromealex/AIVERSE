# 99 — Multi-selection, marquee, Hierarchy filters / hide / lock / hover, asset drop

Date: 2026-11-21.

## Native (`StudioSession`)
- Selection = primary `selected_` (always last) + `selection_`; `studio.select {id|ids, mode:
  replace|add|toggle}`; snapshot `selectedIds`. Every structural edit resets it
  (`selectOnly`/`clearSelection`).
- `studio.entitiesDelete` / `studio.entitiesDuplicate {entityIds}` — one undo step
  (delete highest index first; `MapDoc::mergeUndoSince`); `studio.setProperties` accepts
  `entityIds` (group edit, one undo).
- `studio.entityFlags {entityIds, hidden?, locked?}` — editor-only (not saved), hidden =
  not drawn / not picked, locked = not picked; positional ids are pruned after structural
  edits (`dropOrdinalFlags`). Snapshot rows carry `visible`, `locked`, `owner`.
- `studio.assetPlace {asset, x, z}` — new authored entity (Transform + StaticModel) at a
  ground point; type = first of prop/props/decor/decoration/scenery/doodad/static in
  rules.json, else the selected entity's type; otherwise a clear error.

## Native Scene / host
- `StudioMetalScene::setEditorState(selection, hidden, locked, hover)` every frame:
  extra selected objects outlined, hover outlined lighter, hidden skipped in draw + pick.
  `idsInRect` + `setMarquee` (normalised) for rectangle selection; `groundPoint`.
- `Main.mm`: plain LMB drag on empty space = marquee (Shift add, ⌘ toggle); Shift/⌘ click;
  hover via tracking area (pick ≤ 20 Hz) → `viewport.hover`; Hierarchy hover →
  `viewportInput {kind:'hover', id}`; Delete / ⌘D in the Scene act on the selection;
  the Scene view accepts drags of text `aiverse-asset:<path>` (Assets panel models) →
  `studio.assetPlace` (errors appear in the Assets panel). The game view refuses drops.

## Web
- Hierarchy: Shift range / ⌘ toggle / ⌘A, filters `t:` `owner:` `is:hidden|locked|selected`
  (`hierarchyFilter.ts`), eye / lock buttons and menu items (apply to the selection),
  hover sync both ways, "N selected" footer; delete/duplicate act on the selection.
- Inspector: several selected → common components, differing values "—" (number
  placeholder, indeterminate checkbox, "—" option); an edit writes all objects in one undo.
  Add/Remove Component disabled for multi.
- App: Edit › Duplicate/Delete and ⌘D use the whole selection.

## Checks
- tsc clean; `npm run test:web` pass (new `hierarchyFilter.test.mjs`, merge tests);
  Python contracts 44/44; clang++ 18 syntax: StudioSession.cpp, StudioMetalScene.cpp,
  StudioSessionTests.cpp (new multi-select / group / flags / assetPlace checks — run on Mac).
- Not verified on the Mac: marquee feel, hover tracking, HTML→native asset drag (WebKit
  must put the text on the drag pasteboard — expected, not yet observed).
