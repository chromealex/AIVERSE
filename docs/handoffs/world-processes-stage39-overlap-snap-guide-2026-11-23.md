# AIVERSE World Processes Stage39 — physical neighbor overlap and snap guide
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched. Draft PR #112 not merged.

## Implemented
- `apps/studio-native/StudioWorldProcessesEditing.h`: `snapNeighborEdge` now rejects neighboring EVOLVE rectangles whose perpendicular edge intervals do not overlap with positive length. Returns the shared projected segment (world metres) as `guideStart/guideEnd` along with stable target ID and snapped edge coordinate. The prior nearest-distance / stable-ID tie-break is retained.
- `apps/studio-native/StudioHost.h`: drag scratch contains ephemeral `neighborSnap`, `snapTarget`, `snapGuideA/B`. No authored state or ECS duplication.
- `apps/studio-native/StudioHost.cpp`: draws a highlighted guide along the overlapping neighboring edge and labels the stable target ID while Ctrl/Command snap is active. No neighbor -> 1m grid fallback. Guide disappears on drag completion or cancellation.

## Semantics
Only authored, enabled `EvolveRegion+EvolveArea` targets are considered. Positive-length overlap on perpendicular axis is mandatory; mere corner contact is excluded. The snap coordinate is the neighbor's X/Z boundary; the guide is the actual intersection of the two rectangles' perpendicular spans. Only the dragged owner's authored area changes on release through existing MapDoc RPC.

## Verification
GitHub accepted source commits. No C++ build, Studio runtime, screenshots, Undo/Redo, reload, ECS tick, or CI run performed (user deferred tests). Source remains unverified.

## Remaining
1. Batch group resize with shared/individual pivot, atomic Undo.
2. Editable numeric Scene fields and optional snap configuration.
3. Inspect guide alignment at extreme coordinates and preview/commit rounding consistency.
4. Compile and run real native QA when authorized; keep PR #112 draft until accepted.

## Commits
`1d40c961` overlap filtering
`7e47762c` drag guide state
`8c4a6d4b` visual snap guide
