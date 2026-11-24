# Open World W1.20a — legacy vehicle backlink index

Branch: `feature/aiv20261123_open_world_audit`, PR #116 (keep Draft). Implementation commit `eeab3a69680e03db102bb8bb4fb8ac7527b9e341`.

## Implemented
`engine/actor/ActorRegionOwnership.cpp`: enumerate cars once, validate IDs, build a deterministic map from Vehicle.id to live CharacterBody backlinks during the existing validation traversal. Reuse the index for legacy seat groups instead of scanning all characters for every car. Preserve driver slot checks, packed-ID sorting/deduplication, typed VehicleDrive adapter, collision admission, transfer and tombstone behavior. No public API or persistence schema changes.

Legacy backlink gathering changes from O(V*C) to O(C log V + V log V + P), excluding group sorting and typed adapter costs. The map is transient and rebuilt per reconciliation, so rollback needs no cache invalidation.

## Verification
Source inspection only; native build and tests not run at owner's request. No PASS claimed.

## Next
W1.20b: design opt-in ECS mutation tracking covering World add/remove/destroy/get/tryGet/each and direct Pool access, parallel writes, COW and restore. Mutable reference escape means access notification alone cannot prove a complete change stream; retain compatibility scan until all writers are covered. Add deterministic dirty-only reconciliation with atomic vehicle group expansion and a fallback for untracked writes. Update existing open-world skill and catalog when the API is available. Then implement disk WAL. Do not merge before native acceptance.
