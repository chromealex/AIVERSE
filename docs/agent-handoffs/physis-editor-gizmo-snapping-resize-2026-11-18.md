# PHYSIS Stage 20 — Scene radius gizmos, grid snapping and Undo-safe authoring (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Working branch:** `feature/physis-nature-simulation`, draft PR #25  
**Main integration baseline:** `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` (already an ancestor; no new main merge necessary when this stage started)  
**Previous handoff:** `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md` (Stage 19, HEAD `6eccd89f134e38a6216688829ffa7d9c73d56a02`)

## Actual commits

1. `b2c8796032d29c4f92223c15aeceec96973dc1db` — Stage 20 code, authored tests, PHYSIS documentation and agent rules.
2. `0860f27e9e347a8aa7f028b7ed2aa82ee8a5e1c8` — correction of ImGui Combo's required NUL-separated options, prevention of unintended Y snapping during X/Z translation, and FPS-map radius Undo fixture.
3. **This file:** documentation-only handoff commit; use `git log -1 -- docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md` to identify its commit SHA.

## Scope and source files

- `apps/editor/PhysisFieldEditor.h`
  - Tool selector **Move** / **Radius** in the existing genre-neutral PHYSIS field panel.
  - Selected field's world-space X/Z perimeter is projected by the current Scene camera. The radius handle sits on the +X perimeter, with priority over centre-marker picking in Radius mode.
  - Mouse dragging the handle raycasts to the horizontal plane at the selected field's Y to calculate a new Euclidean radius. No rotation or volume-height editing is implied.
  - Move keeps the prior click/drag X/Z and Shift+Y behaviour. A raw height accumulator avoids losing sub-grid pixel changes when height snapping is enabled.
  - Optional **Snap to grid** for the axes the user is actively changing. Step is adjustable from 0.25 to 1000 metres, not serialized. Moving X/Z does **not** silently alter Y.
  - Gizmo remains opt-in, is disabled while playing or markers are hidden, and does not steal normal Scene selection unless a PHYSIS marker/handle was hit.
- `apps/editor/PhysisAuthoring.h`
  - `snapMeters(value, step)` rounds positive and negative coordinates to nearest metre-grid multiple, bounded to the existing authored coordinate extent.
  - `resizeField(doc, id, radiusMeters, firstEdit, error)` changes `outerRadiusMeters` for storms, `radiusMeters` for wind/gravity. Validates the **entire** map with `parseFieldAssets` before any mutation.
  - First actual radius edit calls `MapDoc::beginEdit`, all subsequent edits during one drag call `MapDoc::touched`. No-op/invalid edits add no undo snapshot; unknown extra JSON field keys remain intact.
  - Storm eye radius remains untouched and native validation enforces `outerRadiusMeters > eyeRadiusMeters`. Native fixed-point range and max radius remain authoritative.
- `tests/physis_field_authoring_test.cpp`
  - Added source fixtures for negative-coordinate snapping, step sizes, field movement, radius serialization, storm-eye validation, radius bounds, NaN/infinity rejection, revision invariants, single Undo/Redo for a whole resize drag, and FPS map resize Undo/Redo.
- `docs/physis.md` — updated Stage 20 editing guide and ownership limits.
- `engine/physis/AGENTS.md` — editing invariants and references for the next agent.
- **This handoff** — checkpoint for continuation.

## Generic engine architecture and ownership

The runtime API remains **unchanged**: the existing `PhysisProjectConfig.h::installFromMap(sim, map)` imports authored assets into ordinary, sorted-id ECS `WindZone`, `StormEvent` and `GravityField` entities.

The editor only writes map-level `physis.windZones[]`, `physis.storms[]` and `physis.gravityFields[]` JSON assets. No live World writes, Box3D mutation, runtime entity-ID coupling, genre-specific objects or new persisted component schema have been added. PHYSIS owns nature/climate/wind/storm/gravity input. **NEREID** owns canonical water; **TerraWeave** owns terrain and vegetation; **Box3D** owns rigid-body dynamics. These separate agents can read PHYSIS through existing read-only sampling. Keep `engine/physis` free of ImGui, FPS, RTS, Terra and Water includes.

## Verification — do not misrepresent

- **PASS:** GitHub connector accepted both implementation commits; source text re-fetched; corrected tool selector has C++ `"Перемещение\\0Радиус\\0"` (NUL-escaped options), and the new test function is registered in the existing test main. Source braces were checked when staging the first commit.
- **NOT RUN:** native C++ compilation and linking, CTest execution, GitHub Actions CI results, editor launch, macOS mouse/camera/viewport visual QA, Undo/Redo exercised in a real editor, Box3D/runtime integration, real native save/load replay.
- Do not label source test additions as passed or change PR #25 from draft before actual verification.

## Exact next steps for the next agent

1. Pull the current branch, then run `./run-tests.sh --filter '^physis_'`; fix native compile or test failures. Verify actual `aiverse_editor` build including `aiverse_physis_field_authoring_test`, not just PHYSIS core tests.
2. Open the editor on macOS, create a regular map and an FPS-verbatim map PHYSIS block, and select one WindZone, StormEvent and GravityField. Test both gizmo modes, snapped X/Z move, Shift+Y with a non-grid starting Y, radius resize (including storm eye limit), overlapping markers, camera tilt and zoom, Undo/Redo, file roundtrips, and disabled/playing modes.
3. Inspect the shared editor's recent concurrent merges before changing Scene/Inspector code; `feature/physis-nature-simulation` currently contains the main baseline cited above. Avoid replacing another agent's work.
4. After native verification, next feature candidates: selected handle occlusion/depth ordering and drag axis visualization; transform multi-select and duplicate fields; optional authority-safe live hot-reload with ID mapping (must define persistence and rollback semantics before implementation).
5. After every subsequent stage, commit an updated standalone handoff with actual code SHAs, files, API, test evidence and next task.

**Known limitations:** radius ring is a projected horizontal circle, not a true 3D volume gizmo; only a +X handle is provided; no multi-selection, rotation or scene-depth occlusion; snaps use world-origin coordinates, not PHYSIS grid-cell origin; authored source hot-reload, native field ID ECS mapping and network replication are not implemented. Large field counts still require profiling.
