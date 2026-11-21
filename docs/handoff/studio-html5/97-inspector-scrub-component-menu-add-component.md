# 97 — Inspector: drag-to-scrub, component menu, Add Component, missing materials

Date: 2026-11-21. Design board 2 ("Инспектор").

## Done
- **Scrub labels** (`PropertyField`, `propertyLogic.scrubValue`): number labels are drag
  handles — 1 px = 1 step (rotations 1°), Shift = ×0.1, Alt = snap (15° rotations,
  1 unit otherwise), clamped to min/max; one commit on release = one undo step.
  X/Y/Z tags are coloured red/green/blue.
- **Component ⋮ menu** (also right-click on the header): Reset, Copy Values, Paste
  Values, Remove Component (`panels/inspectorActions.ts`). Reset keeps position, sets
  rotation 0 / scale 1; Mesh Renderer reset = built-in cube + default materials.
  Paste writes only editable keys that differ, same component type.
- **Add Component** popover with fuzzy search over `studio.componentCatalog`:
  Mesh Renderer (native add, promotes legacy units to an authored Transform), Point
  Light (creates a light entity at the entity's X/Z via `studio.lightAdd {x,z}`), NEREID
  Water (opens its panel), Transform/Unit listed as unavailable with the reason.
- **Missing material** slot values show "(missing)" plus "Create “name”" (matte
  material via `studio.materialCreate`) and "Use default".
- **Native** (`StudioSession`): `studio.setProperties` (many keys, ONE undo step; any
  failure restores the document — `MapDoc::undoDepth/mergeUndoSince/abandonSince`),
  `studio.componentCatalog`, `studio.componentAdd` (MeshRenderer), `studio.componentRemove`
  (not Transform/Unit/NereidWater), `promotedAuthored()` shared with setProperty.
  Main.mm sends `document.changed` for these and for `studio.entityMove`.

## Checks
- tsc clean; `npm run test:web` all pass (new `tests/inspectorActions.test.mjs`, scrub
  cases in `propertyLogic.test.mjs`); Python contracts 41/41.
- g++ `-fsyntax-only` StudioSession.cpp, MapDoc.cpp, StudioSessionTests.cpp (new batch /
  rollback / catalog / add-remove-undo checks). **Not run** — needs the Mac build.

## Not done
- Multi-selection "—" mixed values: stage 99 (needs native multi-select).
- Model thumbnails in the asset picker (no offscreen preview renderer exposed to web).
