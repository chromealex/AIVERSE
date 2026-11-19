# PHYSIS Stage 22 — Marquee selection, pivot modes and 3D gizmos (2026-11-18)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
Pull request: #25, DRAFT
Previous stage: docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee (already included in branch history)

## Implemented commits

- a05830a60537db14fcab3d45208808ac8c999240: five editor pivot policies in PhysisAuthoring.h; new pure PhysisGizmoGeometry.h; added source fixtures.
- c26a05daa8980c4ac78375429e5d98bba12ce210: rectangle selection and pivot controls wired to PHYSIS Scene and numeric transform operations.
- a233310bb853bab76b4e821e3f152b66c5c0fea7: projected X/Y/Z axes with true 3D RTSCamera::screenRay / closest-ray-axis world movement.
- a792000e23911cd76ba28c02945af984fb5b45a8: transformFields supports a frozen source JSON and firstEdit/touched for multi-frame drag, plus new source tests.
- ef33a75ada69bd18a967204b44dda0befa8b43c1: draggable horizontal rings for yaw about Y and uniform geometric scale of selected assets.
- ab66c093cfa43491aef086e3fc6c6bfa13e21310: reverse marquee visual rect, selected-only pivot hot-path checks, more edge-case test fixtures.
- This handoff commit: inspect git log -1 -- docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md

During the stage another agent added unrelated engine/physis/PhysisFieldChangePlan.h and docs/agent-handoffs/physis-field-change-planner-2026-11-18.md via commits d4bed2b5 and 717b7b50. The branch was re-read after this concurrent update; no files were overwritten. That pure field change planner is NOT a working ECS reconciler or editor hot reload.

## Editor API and files

- apps/editor/PhysisAuthoring.h: FieldPivotMode {Active, Median, BoundsCenter, WorldOrigin, Custom}; FieldPivot{x,y,z}; computeFieldPivot(config,ids,activeId,mode,custom,out,error) returns a deterministic pivot for a selected authored set. Missing/duplicate IDs and invalid selected coordinates are rejected. Pivot settings are transient editor state, not serialized ECS/map schema fields.
- apps/editor/PhysisGizmoGeometry.h (NEW): idsInScreenRect normalizes rectangles and selects sorted unique projected centre IDs; closestRayAxisParameter finds closest point from 3D camera ray to infinite X/Y/Z world axis, rejecting parallel/behind-eye solutions; segmentDistanceSq is for handle picking only. No ImGui, game module or ECS writes.
- apps/editor/PhysisFieldEditor.h: opt-in marquee on empty Scene, Ctrl/Shift additive selection, five pivot modes, projected X/Y/Z axis handles, horizontal gold yaw ring, cyan uniform-scale ring, active-only radius editing. Normal Scene picking remains when PHYSIS edit mode or marquee is disabled.
- apps/editor/PhysisAuthoring.h: transformFields now optionally takes baseline pointer and firstEdit flag. Ring drag computes ALL frames from initial authored JSON rather than progressively transforming already rounded results; first effective write begins one MapDoc Undo snapshot and following frames call touched. The editor cancels ring drags on unexpected document revision changes. Each candidate validates the full PHYSIS field config before write.
- tests/physis_field_authoring_test.cpp: authored pivot cases, missing/duplicate IDs, reverse/invalid rectangles, ray-axis near-parallel/behind-eye checks, frozen-baseline drag and single Undo/Redo tests.
- docs/physis.md, engine/physis/AGENTS.md, root AGENTS.md, skills/world/aiverse-physis/SKILL.md should describe and link this stage.

## Semantics and invariants

The selected set may mix WindZone, StormEvent and GravityField. Moving along a world X/Y/Z axis changes their centres by the same world-space delta. Axis snapping applies to the pivot, preserving every relative offset. Dragging the yaw ring rotates field centre positions in the horizontal X/Z plane about the selected pivot. It DOES NOT rotate wind vector, vortex force, gravity acceleration or field orientation: no authored quaternion orientations exist yet. Uniform scale changes group spacing and volume extents (wind/gravity radius, storm eye radius, outer radius, height), not natural force strengths. Radius mode remains active-field-only.

All operations write only editor MapDoc authored field arrays with existing native parseFieldAssets validation. They DO NOT modify live Simulation World, create Box3D physics changes, write NEREID water or TerraWeave terrain/foliage, or depend on FPS/RTS genres. No new persistent component types or schema migrations.

## Validation status

PASS: GitHub connector accepted implementation commits. Relevant source was re-fetched, source-level patch anchors and balanced braces inspected. Test cases were authored and registered within the existing physis_field_authoring CTest target.

NOT RUN: native C++ build, CMake/CTest, sanitizer, macOS graphical editor click/drag QA, integration save/replay, Box3D/Water/Terra runtime. Local environment has no AIVERSE checkout and cannot resolve github.com; native testing was not possible here. GitHub Actions PHYSIS jobs were observed QUEUED, e.g. 44390895402 for ab66c093, not PASS.

## Limits and next steps

- Rectangle uses projected W/S/G marker CENTRES, not volume extents; visibility respects Scene viewport but no depth buffer occlusion.
- X/Y/Z move uses true camera ray closest-axis geometry and rejects near-parallel rays. Camera still uses float world-space projection without origin rebasing.
- Yaw is shortest-angle wrapped around Y (about +/-180 degrees per drag), not full three-axis quaternion rotation. Uniform scale does not support non-uniform axes. Horizontal-plane ring interaction may fail at near-horizontal camera pitch.
- Revalidate full config on each effective ring drag; profile editor cost at ~2048 fields.
- Check source tests with native checkout: ./run-tests.sh --filter '^physis_'. Also build aiverse_editor and aiverse_physis_field_authoring_test; run CTest physis_field_authoring.
- Mac QA: standard and FPS maps; box in all drag directions; add vs replace; all pivot policies; axis drags with ortho/perspective cameras, degenerate view; yaw/scale Undo/Redo; invalid bounds and external edit; map reopen; make sure normal Scene selection still works when opt-in tools off.
- Further work: depth-aware handles, origin rebasing, orientation-aware per-object transforms only after an explicit ECS schema, and server-authoritative tick-boundary reconciler (independent field-change-planner agent).
- Keep PR #25 draft until actual compile/CI and editor QA. After EVERY next code stage, commit a standalone handoff with actual tests PASS/FAIL/NOT RUN, SHA and precise next steps.
