# PHYSIS Stage 22 — deterministic field change planning (2026-11-18)

Repository: chromealex/AIVERSE. Branch: feature/physis-nature-simulation. Draft PR #25.
Implementation commit: d4bed2b5b3fcd0084438c0c1560ce5c1603c8b19.

## Done
Added engine/physis/PhysisFieldChangePlan.h. Pure header-only planFieldChanges compares two lists of stable authored IDs, kinds and canonical JSON fingerprints. Sorts both inputs by ID, rejects duplicate/empty IDs, emits deterministic Add/Remove/Update operations. A field changing kind is removed and re-added. Unchanged fields produce no operations. This planner is genre-independent and does not mutate World, save schema, or map documents.

## Contract
The host must provide canonical serialized JSON (same key order/normalization on both sides) and validate assets with parseFieldAssets before planning. The planner does NOT apply ECS changes, allocate entities, synchronize multiplayer, or perform rollback. No live editor hot reload is claimed. On authoritative host, apply at a simulation tick boundary only after validating complete new map and resolving stable ID-to-Entity ownership; snapshots and replication must be updated together. Do not mutate World during read-only presentation iteration.

## Verification
GitHub file creation succeeded. No C++ compilation, CTest, CI, GUI test or runtime integration performed in this session. Do not merge PR #25 until validation.

## Next
Implement a tick-boundary reconciler with stable authored ID -> Entity mapping in rollback-owned ECS resource; atomically apply plans after full validation and preserve entity IDs on Update. Add tests for deterministic ordering, type replacement, duplicate IDs, save/replay, network authority and editor undo/reload. Integrate with PHYSIS Inspector only after these tests. Coordinate memory-budget PR #28 without blindly merging divergent branch history.
