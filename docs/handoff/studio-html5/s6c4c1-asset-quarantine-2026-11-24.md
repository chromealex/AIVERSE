# S6c.4c.1 — asset soft deletion and quarantine (2026-11-24)

Branch `feature/studio-html5`; base `fff8835f`; native implementation `bf10807cdf8285c43112e3bd87ca80a11563ff81`.

- Native confirmed deletion binds project, asset history version, GUID and relative path. Move asset + root sidecar outside content into a unique .aiverse-cache/studio-asset-trash/<id>/asset pair, with original path/GUID/tree digest in record.json. Folder descendants and payload remain intact. Never purge backup on history eviction/reset.
- Replay uses existing SHA-256 tree identity, existing-only catalog, collision checks and journal v2 (restricted project-relative content/trash pairs; no material restore for v2). Undo restores original path/GUID; redo repeats known map/rules/material reference check. Native updates Assets/History. UI context menu Move to Trash with explicit confirmation and typed bridge.
- Test sources: folder/child GUID, restore collision, reference refusal, persistent quarantine on eviction, journal partial recovery without touching unrelated materials.

Limits: session-local undo stack; backup remains on disk, no trash browser, persisted undo stack, disk quota/purge policy, standalone MCP or full script/binary dependency scan yet. Recovery record is for manual recovery, not an automatic cross-session restore endpoint. Journal POSIX durability and concurrent external writer limitations remain. No native/web builds or tests executed. No main merge.

Next S6c.4c.2: reversible import/create with staged payload and sidecars; S6c.4c.3 replace preserving target GUID and old/new payload; trash recovery UI/retention policy; S6c.5 project document crash recovery; S6d agents.
