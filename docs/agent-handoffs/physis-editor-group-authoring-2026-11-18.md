# PHYSIS Stage 21 — multi-selection, grouped transforms, duplication and deletion (2026-11-18)

**Repository:** chromealex/AIVERSE  
**Branch:** feature/physis-nature-simulation  
**PR:** #25, DRAFT. Do not mark ready before native tests and editor QA.  
**Main baseline:** 9ee6ff6fbf390bd45363bc92c838f492dfe92eee (an ancestor; no main merge needed at Stage 21 start).  
**Previous handoff:** docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md (Stage 20, last head 7cffd3441bd0e869f15f44bdad9d5b7b4047bd42).

## Actual commits, in order

1. 1cf1003e2645bba6a817ba27cd574f59041031ba — apps/editor/PhysisAuthoring.h: atomic batch movement, duplication and deletion; tests/physis_field_authoring_test.cpp: added group tests.
2. b77d8d3b53756df1f629e91e8365c37cbe233753 — apps/editor/PhysisFieldEditor.h: sorted multi-selection and selected marker rendering.
3. f84e4c14a68d3d2ae910a45bb8abcd4d0e2696aa — editor Scene mouse control for group translation using immutable drag origins.
4. b43105b1175c58e32419620fbaa675476e25c05a — editor field-list selection, group clone/delete actions and Inspector affordances.
5. 62437b0203fe6bc27cef0a06b57e376b47c51431 — preserved already-transformed X/Z and Y axes when Shift is toggled during drag.
6. 19aa5dc7ac3dc194fc85717385b11cc11a382a5d — PHYSIS documentation, root/engine AGENTS rules and the reusable AI skill.
7. 2737a95de2fdc0278384558037b9ff0bed4e0beb — atomic group yaw/scale in authoring API; source tests.
8. d20a3d303062025d2d8e1c3c831b88847b6aa408 — numeric group yaw/scale controls in PHYSIS editor; invalid-scale test correction.
9. e3c4ea5aca5aa921860ade7fb0c2ed756c52a2e5 — updated documentation for yaw/scale, agent rules and explicit utility include.
10. This handoff is a separate documentation commit after those sources. Use git log -1 -- docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md for its SHA.

## Architecture and public authoring API

All batch operations are editor-only in apps/editor/PhysisAuthoring.h. They update map-level physis.windZones[], physis.storms[] and physis.gravityFields[] in MapDoc; they do NOT create/change live ECS entities, Box3D bodies, NEREID water or TerraWeave assets. They reuse strict shared native parseFieldAssets validation.

    struct FieldPosition { std::string id; double x, y, z; };
    bool physis_authoring::moveFields(
        MapDoc& doc, const std::vector<FieldPosition>& absoluteTargets,
        bool firstEdit, std::string* error = nullptr);
    bool physis_authoring::duplicateFields(
        MapDoc& doc, const std::vector<std::string>& ids,
        double dx, double dy, double dz,
        std::vector<std::string>& newIds, std::string* error = nullptr);
    bool physis_authoring::removeFields(
        MapDoc& doc, const std::vector<std::string>& ids,
        std::string* error = nullptr);
    bool physis_authoring::transformFields(
        MapDoc& doc, const std::vector<std::string>& ids,
        double pivotX, double pivotY, double pivotZ,
        double yawDegrees, double uniformScale,
        std::string* error = nullptr);

- moveFields expects ABSOLUTE positions for all selected fields, not cumulative deltas. The Scene records its original position snapshot on click, computes a common delta from the clicked active pivot and asks moveFields to write all targets atomically. On first effective movement MapDoc::beginEdit creates the single undo snapshot, and subsequent frames call MapDoc::touched. Duplicate/missing/invalid IDs, invalid bounds or complete-config parser errors reject the entire batch BEFORE MapDoc mutation.
- duplicateFields sorts requested IDs, keeps original JSON and custom/unknown keys, generates deterministic collision-free authored IDs with -copy or -copy-N suffixes within the 64-character ID limit, offsets X/Y/Z and creates one Undo transaction. The parser still enforces a total 2048-field cap and matching persistence-enable flags. New IDs are map keys, NOT runtime ECS IDs; native installFromMap still sorts authored IDs before spawning.
- removeFields deletes a mixed-type selection in one transaction. Any unknown/duplicate ID invalidates the whole action, no partial deletion.
- transformFields rotates X/Z positions around explicit Y-axis pivot (angles in degrees) and uniformly scales XYZ distances and geometry radius/extent in one validated transaction. Wind/gravity radius, storm eye radius, outer radius and height change; velocities, storm strength, gravity force and field IDs DO NOT. All output coordinates are quantized to 3 decimal metres in the authored JSON, then read into Fixed by runtime installation. Bad group, angle, size, coordinate or parser bounds aborts the whole operation.

## Scene, Inspector and state

apps/editor/PhysisFieldEditor.h owns the transient vector of sorted selected authored IDs and one active pivot. The panel supports Select All / Clear; Ctrl or Shift plus click on a marker or row toggles membership; a plain list click selects one field. With Move active, dragging any previously selected marker moves all selected fields. The active member becomes the drag pivot; optional metre snap is applied to that pivot ONLY to preserve relative offsets. Shift during drag applies group Y delta; finished axes are preserved if modifier mode changes.

The Radius gizmo is intentionally active-field-only. When more than one field is selected, individual typed/JSON property forms do not edit a surprising member; they prompt the user to select one. Source field ring rendering is capped when very many fields are selected, but all selected centre markers remain highlighted.

The panel additionally exposes Duplicate Selected and Delete Selected, and numeric Group Yaw (degrees) + Uniform Scale followed by an explicit Apply button. Pivot is the active field's center, all changes are one MapDoc transaction. Runtime ECS state, gameplay selections and native save component schema are unchanged. No per-genre modifications to FPS, RTS or their World systems.

Other touched files:
- tests/physis_field_authoring_test.cpp (additional tests, existing CTest target)
- docs/physis.md
- engine/physis/AGENTS.md
- skills/world/aiverse-physis/SKILL.md
- root AGENTS.md
- this handoff.

PHYSIS owns deterministic nature/climate/weather/wind/storm/gravity simulation. NEREID owns canonical water and TerraWeave owns terrain/foliage, while Box3D owns rigid body physics. Do not merge or duplicate these authorities.

## Validation status, explicitly factual

- **Source/GitHub operations PASS:** all listed source commits were created in the connected GitHub repository and branch refs were updated. Source was re-fetched to inspect batch functions and CTest source. Basic balanced-brace checks were run during authored patches; this is NOT a compile test.
- **Test source AUTHORED, NOT RUN:** group mixed-type translation with whole-drag Undo/Redo; no-op and atomic rejection for missing/duplicate/out-of-bounds input; mixed-type duplication and preserved unknown keys; collision-free repeat copies; atomic delete/Undo; FPS-verbatim map/group save-roundtrip; group yaw+scale geometry, Undo and rejection on invalid scale/ID. The existing CTest physis_field_authoring target must run.
- **Native build NOT RUN:** no checkout/compiler build completed during these connector-based edits. No CTest PASS, no warnings scan, no sanitizer results, no macOS Scene visual/drag QA.
- **GitHub Actions:** PHYSIS workflow runs were observed QUEUED, not concluded (e.g. branch run 44339308799 for earlier documentation SHA 19aa5dc7). Do NOT claim CI PASS for Stage 21.

## Next agent: required concrete steps

1. Update checkout of feature/physis-nature-simulation, inspect concurrent editor merges and rebase/merge main ONLY if it has moved. Compile C++ and editor. Run:
   
       ./run-tests.sh --filter '^physis_'
       cmake -S . -B build/physis-editor -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
       cmake --build build/physis-editor --target aiverse_editor aiverse_physis_field_authoring_test aiverse_physis_field_assets_test aiverse_physis_project_config_test -j4
       ctest --test-dir build/physis-editor --output-on-failure -R '^physis_(field_authoring|field_assets|project_config)$'

2. On macOS start editor. Create regular and FPS maps with PHYSIS, mixed W/S/G sources, Ctrl/Shift multi-select in Scene and list, group drag via each selected pivot, toggle Shift mid-drag, test snap on/off and one Undo, duplicate mixed selection twice, delete copies, restore Undo/Redo, resize active only, group rotate 90 degrees, scale 2x, invalid size rejection, save/reopen and compare JSON. Ensure normal project FPS/RTS selection works outside PHYSIS marker hits.
3. Inspect screenshots for small/large camera zoom and tilt, clipped rings, handle overlap and list selection controls. Measure large group drag cost: moveFields rebuilds/revalidates full config on each effective mouse update; don't advertise large-world editing performance before profiling.
4. New features only after validation: drag rectangle, orientation-aware true 3D gizmos and pivot modes, field asset authoring IDs mapped to ECS at live hot-reload with server authority/rollback/persistence migration. These are NOT implemented.
5. Commit an individual new handoff per following stage with actual result PASS/FAIL/NOT RUN, changed files, commit/branch, architecture, remaining limits and next step.

**IMPORTANT:** Leave PR #25 as draft until real native/test evidence is available. All tests described above are authored but not executed.
