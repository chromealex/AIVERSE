# World Processes Stage53 — single-area parity and native shutdown
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 remains draft.

## Source changes
- StudioSession.cpp: single-area authoring now rejects locked/hidden entity IDs. Single-area and batch-move world bounds use the shared evolve::scale_math::withinBounds helper already used by batch resize.
- StudioWorldProcessesEditing.h: single-area preview validates typed EvolveArea, rounds to integer centimetres, uses the authoritative half-extent constant rather than hard-coded maximum, and uses shared world bounds. One-axis group preview uses the shared axis transform and bounds checks.
- StudioHost.cpp: shutdown clears host-local numeric preview, owner, selection signature, revision, custom pivot and drag scratch even if the webview does not emit pagehide. No new persisted state.

## API and ownership
Existing Studio RPC names and MapDoc undo transactions are unchanged. Native shutdown cleanup is local to the StudioHost being destroyed; it does not guarantee cleanup of a preview hosted by a different native window. HTML5 pagehide and owner-scoped clear remain in place.

## Verification
Source commits accepted by GitHub: 7e958d6f, adcee34a, db4d799c.
No C++ compilation, TypeScript typecheck, Vite build, CTest or runtime interaction was performed (user deferred QA).

## Remaining
Audit multi-window cross-host forwarding and preview/commit eligibility, including offset limits and selection locks. Verify all previous stages together; fix compile/runtime issues before merge. This is not a claim of feature acceptance.
Next: focused integration audit of native RPC, typed HTML5 bridge and selection lifecycle, then acceptance when authorized.
