# S6c.3 — HTML5 project history panel (2026-11-24)

Branch `feature/studio-html5`; S6c.2 base `7bf1d715`; implementation `185074ffccbf40542ef974b0e465f73692091ce0`.

## Implemented
- Existing HistoryPanel now has explicit Map / Project scope using shared Studio UI buttons, alerts, skeleton and empty state. Map retains direct position navigation; project rows show applied/redo/current state without clickable jumps. Explicit project Undo/Redo uses native permission flags and exact version. Refresh, pending, conflict, stroke-active, owner-blocked, empty states and retained steps/payload counters.
- Typed method/params/results and conservative browser mock. One in-flight action per panel; reads invalidated on snapshot/scope changes; late navigation result does not replace a newer snapshot. Error persists through automatic reread until explicit retry/scope switch. Native host remains the authority across windows.
- Native snapshot exposes project.historyVersion. Rules panel reloads on version changes (raw editor resets to avoid saving stale JSON); Build refreshes identity/estimate without resetting build log on history changes. Existing snapshot-driven tool/fog views refresh. Native host notifies document change at stroke start; native mutation guard still rejects concurrent edits. Global map shortcuts unchanged.
- Test source: project permission/opaque-version helper, native bridge conflict, demo refusal, snapshot version contract. test:history added to test:web.

## Boundaries
Separate histories, no project multi-step jump, no asset operations or combined agent undo, no process-crash durability. Rules raw draft is reset when owner history version changes; future draft conflict UX can preserve text for manual rebase. Native snapshot/event timing and all dependent custom panels need integrated verification. Retained counters are estimated payload, not process RSS. No screenshot or production-readiness claim.

## Verification
Remote implementation diff reviewed. Added tests but did not run native C++ tests, npm typecheck/build/test:web, macOS WKWebView screenshots or Mac/KINGSTON acceptance. No main merge.

## Next
S6c.4: typed reversible asset metadata and content move plans with GUID/path identities, conflict checks and owner validation. Do not treat the move crash journal as user history. Start with metadata patch and preserve reference integrity; then content move/import. S6c.5 durable multi-file recovery; S6d agent history integration.
