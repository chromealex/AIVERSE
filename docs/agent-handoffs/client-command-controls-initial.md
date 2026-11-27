# client-command-controls — initial handoff

Owner: gameplay
Branch/SHA: pending
Architecture/API: pending
Changed paths: pending
Tests: NOT RUN
Risks: pending
Next: confirm owner, update roadmap and implement.

## 2026-11-27 — implementation checkpoint

Commit: `8be0f0c0d17500ced8dfaf76b5e91fa7471340c6`
Branch: `codex/issue223-minimap-atlas-20261126`
Paths: `apps/client/ClientUi.cpp`, `apps/client/GameClient.cpp`, `apps/client/GameClient.h`, `engine/assets/lang/en.json`, `engine/assets/lang/ru.json`, `engine/assets/ui/hud.rml`, `engine/assets/ui/menu.rml`, `engine/platform/EnhancedInputPresets.h`, `tests/libraries/enhanced_input_config_tests.cpp`

Source changes committed; readiness is pending. Tests/build: NOT RUN for this revision, per request to build the complete batch once at the end. `git diff --check` passed before commit. No save-format migration introduced. Existing atlas-only ImageSource::load semantics preserved; new loadImage handles both storage forms. Remaining work: review and verify controls, Lua selection, voice sequencing, FX and projectile paths; add missing runtime/Studio/publication evidence. Issues #211–213, #219, #221 remain unimplemented; #206 requires Windows crash verification. Do not merge or close issues from this checkpoint. Next: finish remaining issue implementations, refresh main, run the final merged-tree Mac gate and feature readiness checks.

## 2026-11-27 — shared project settings delivery scope

Confirmed missing authoring route for project-level controls, portrait defaults and minimap crop. Extend existing Project panel with a generic game.json object editor, Load/Save/Discard, shared Studio UI controls and no per-feature schema exceptions. Read via game_info; save via set_manifest using the empty root path already supported by jsonPath. Require an object root and compare the original manifest before mutation; native Studio must require expectedProject and the original-manifest guard. Project::saveManifest already validates exact saved disk bytes through applyHistoryTransaction and owns bounded Project history. Preserve this owner and its topology/history barriers. UI must retain failed drafts, reject stale replies after project switch and explicitly describe the existing Project history scope. Do not claim every manifest key has semantic validation.

Acceptance required: save/reopen, unchanged and stale snapshots, malformed/non-object JSON, switched project, external file conflict, Project undo/redo for compatible settings, native WKWebView and narrow-panel visibility. Read-only planning during active full gate32364; no compiled source changed yet. The gate remains live; source edits and targeted rebuild follow its terminal result. Any new source revision requires the final current-tree gate again.
