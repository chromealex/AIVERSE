# S6d.2 — agent capability policy and internal moves (2026-11-24)

Branch `feature/studio-html5`; base `f6e1aa5e`; implementation `750bb8e480be7e018a977fc6b102ebc0015c1862`.

- Separate explicit agent history policy from legacy panel suffix heuristic. Known read/map/project/assets candidates listed by exact operation name; all other operations unverified. Actual retained owner IDs still required; unknown or mismatched scope can report a partial link plus untracked effects and disables map bulk undo. Failed potentially mutating calls fence bulk undo too. Native no-op metadata/move does not invent an edit.
- MCP studio_history_capabilities enumerates the full live Workspace registry with policy; this is an inventory, not evidence every operation has been individually audited. Added Studio-only schemas for GUID move preview and commit. Native adapter resolves existing GUIDs, exact project/version, separate token and explicit decisions, rechecks GUID/path before native transfer, and reuses material repair/dependency checks/journal/history. Browser and agent preview slots isolated; successful agent move invalidates browser preview. No arbitrary file path, external picker, replace/delete/purge adapter.
- Extended authenticated agent fixture with capability inventory, forged browser token, incomplete decisions, move history and undo. Updated protocol docs and skill.

Limits: unverified operations still execute through existing Workspace; no universal reversibility guarantee, no mixed-scope atomic undo, no full audit of non-cooperating external edits or dynamic references. Preview rechecks GUID/path but does not pin all source payload bytes between review and commit. Native/web tests and compilation not executed. No main merge.

Next: integrated QA, compile corrections, macOS/Windows filesystem and frontend checks, operation-by-operation coverage expansion based on actual owner contracts.
