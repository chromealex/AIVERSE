# S6c.1 — project document save boundary (2026-11-24)

Branch `feature/studio-html5`. S6b handoff `5f90be13`; first implementation `c41dc01dfd169f2c209542ef975b92821fe11b1e`.

## Implemented
- Shared editor owner `Project::load` retains exact manifest and composed rules file bytes separately from normalized split-composed text. No-op saves retain original bytes.
- `ProjectFileEdits.h`: explicit reversible before/after plan for existing regular project files; all dependencies preflighted, duplicate or out-of-project paths refused, unchanged files checked but not rewritten. Stage sibling backup and replacement files before publication; verify expected bytes before rename and verify all resulting bytes. On in-process failure reverse completed replacements in reverse order only if their expected after-image still matches. Preserve backup path and report failure if reversal itself fails.
- `Project::saveRules/saveManifest` reuse this persistence boundary; normalized and saved-file baselines advance only on success. `rules_set/merge/delete` restore in-memory document and dirty state after failed save/validation; emit Rules notification only after successful save. `set_manifest` restores in-memory manifest on save refusal. `map_save(addToGame)` reports partial success explicitly instead of silently ignoring manifest save failure.
- StudioSessionTests adds reversible plan, unchanged dependency conflict, owner no-op/comment-format preservation (exact raw bytes), composed rules conflict, successful edit and manifest conflict cases.

## Limits
This is the persistence prerequisite, NOT user-visible project Undo/Redo. It is per-file atomic replacement with in-process rollback, not crash-atomic multi-file persistence, no fsync/journal or external-writer lock. Exact bytes detect observed changes, not adversarial races or identity changes with identical bytes. ACL/ownership preservation beyond basic permissions is not established. Existing-file only; asset moves/import/delete not covered. A rollback failure requires manual recovery from reported staging backup. Other direct owner callers and project creation paths need compatibility review. Manifest changes selecting a different rules file require reopening before subsequent rules save.

## Next (S6c.2)
Implement bounded typed Project document history, separate from MapDoc history. Record only validated successful owner operations with exact before/after file plans, bounded by bytes and count, expectedVersion/source identity, conflict refusal; reload and validate restored manifest/rules via Project owner without dropping map state. Explicit project-scoped UI history navigation; never silently route map undo to project history. Handle rules-file-set/module changes explicitly. Then integrate metadata/content moves and the durable multi-file journal. Do not replay arbitrary JSON commands. Keep agent history honest about uncovered changes.

## Verification
Remote diff reviewed. Native C++ compile, StudioSessionTests, Windows/macOS replacement behavior, staged write/rename failure injection, process-crash recovery, integrated web/agent tests and Mac/KINGSTON gate not run. Do not claim S6 complete or merge to main.
