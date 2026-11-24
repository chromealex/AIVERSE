# S6f.1 — coordinated agent history (2026-11-24)

Branch feature/studio-html5; base c259a583d464ef095f5451ca8cd496a95c2b4f1b. Staged owner plans 9689e179d323dcba7a8006fd8b0467f58f46ce70; native coordinator fde4363d862347916aecd10606cb06d0310cf6bb; UI and intervening-edit fence 097443dfe3bf10bc54ed878fde2bf180983343bc.

- Exact per-owner ordered suffix/prefix planner, max 64 steps; merge consecutive existing-file transitions and check all current baselines. Refactor Project history and metadata owner to validate without publishing.
- Reviewed agent batch undo/redo for fully tracked Map, Project and asset metadata edits. Preview token binds all history versions and exact step order; refuses unsupported moves, untracked calls, failed potentially mutating calls, evictions, partial navigation, project/map switching and intervening owner version changes. Native explicit confirmation required.
- Stage map/project/asset copies, validate metadata identity, commit all existing-file edits including Project checkpoint through one durable document journal, then publish live owner state. Asset checkpoint remains separately fenced and written after commit; live Map changes are unsaved. No claim of cross-owner crash-atomic saved-map/asset history.
- Agent Panel review displays per-scope counts and confirm, disabled reason. Tests cover boundary planner, merged file chains, mixed native batch, stale token, confirmation, external document conflict without map/metadata changes, redo and unsupported move plan refusal.

Limits: existing file operations only, no global journal for renames, created/deleted assets, trash, replacement swaps or saved map sidecars. Full batch does not guarantee rollback of post-commit UI/index refresh failures. Existing noncooperating external writer limitations remain. No build or tests executed for this stage; no main merge.

Next S6f.2: extend coordinator to typed file relocation/quarantine/replacement and durable cross-owner recovery; then S6g dependencies, S6h storage policy, S6i MCP coverage and integrated QA.
