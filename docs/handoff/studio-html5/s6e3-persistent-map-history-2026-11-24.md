# S6e.3 — persisted Map history (2026-11-24)

Branch feature/studio-html5; base f986ae68451da8774eea821380f962b831625994. Codec and MapDoc save/load integration c7f79360ba2b0f72d1223f84b43bedc970bcce95; save marker, labels, UI and epoch fencing 903f5ce5d4e62b9d17b04437062cb220d15b7edc.

- Exact editor-only typed snapshot encoding for all current MapData fields, including float bits, flat terrain, resource fields, road indices, entity/player custom data, planet face layers/detail, and world-space extras. Save up to 200 retained steps and 256 MiB encoded checkpoint. No runtime map format change. Restore saved undo/redo stacks, position and labels only when map bytes and decoded sidecars match. Fresh IDs and epoch on reopen prevent old links/tokens from matching.
- Per-map .aiverse-cache checkpoint and advisory lock, durable dirty marker before save, map/sidecar flush before marker removal. Save As resets timeline and clears planet sidecar names to avoid inherited paths. Existing MapDoc owners and compound brush grouping retained; Studio reads labels from MapDoc and exposes persistence warnings.
- Added test scenarios for precise historical transforms and binary layers, redo cursor, compound labels, external change and interrupted marker refusal, independent Save As, planet and world-space snapshots.

Limits: history persists at explicit/owner map save boundaries, not unsaved work; current state after reopen is the normal saved map representation. Map JSON and sidecar publication itself is still legacy non-atomic; interruption can leave partial map data, not merely a stale history. No cross-owner atomic transaction, no full writer locking against legacy/external changes, no measured performance or large-planet acceptance. Checkpoint failures leave history fenced; they do not silently restore unsaved state. Compilation and tests not executed in this stage. No main merge.

Next: S6f common cross-scope coordinator (do not call sequential undo atomic); S6g dependency coverage; S6h storage policy; S6i MCP coverage; integrated QA.
