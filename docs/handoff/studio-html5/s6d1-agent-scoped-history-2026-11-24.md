# S6d.1 — scoped AI agent history (2026-11-24)

Branch `feature/studio-html5`; base `1613b366`; native `e054b1add49602b988b0aae109207df7bf9b2a2a`; UI `374ab9eae600744084b67f51b9563e437ef5d696`.

- ProjectHistory entries now have stable epoch-qualified IDs; recording links to exact newly retained native steps, not depth. Linked calls capture Map, Project and Assets owner changes, mark other/failed operations untracked, and cap action rows. Project/map source and retained IDs rechecked for every display and navigation. Agent-specific native step requires exact scope/id/version, current boundary and stopped agent; owner validates actual document/asset conflicts.
- Existing map bulk action renamed Undo map run; native rechecks exact retained agent-only map prefix/suffix and refuses mixed/untracked/evicted changes. No global atomic Undo all across owners.
- Shared MCP registry advertises four authenticated Studio adapters: asset history, existing-only catalog, metadata patch and folder creation. Mutations require current project and asset history version and route to native owners. Standalone/legacy fallback errors; no picker, purge, arbitrary RPC or asset delete adapter. Other asset operations remain untracked unless explicitly integrated.
- Agent Panel shows scopes, applied/undone/not-retained/failed states and independent boundary-only Undo/Redo. Refresh on document/agent events. Host refreshes Assets after linked mutations. Added authenticated localhost fixture for project rules and asset folder, stale version, independent Undo/Redo, reopen refusal, demo checks.

Limits: not all Workspace mutations are reversible; suffix-based read-only classification is still legacy and requires an explicit capability audit. Multi-owner calls are not atomic. No bulk cross-scope navigation, linked external import/replacement or permanent deletion. Native/HTML5 build and tests (including socket fixture) not executed; no main merge.

Next S6d.2: audit MCP mutating operations for actual owner coverage and fail-closed classification, add agent links for safe internal asset operations, improve cross-scope review and test integrated conflicts. Then full branch QA.
