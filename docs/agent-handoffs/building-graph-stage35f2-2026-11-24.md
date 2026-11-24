# Stage 35F.2 — native authored route inspection (2026-11-24)

Branch: integration/building-graph-stage35d-20261124. Parent stage 35F.1: 313346e25fcffc14a92a38cdf3db9346f120526c.
Native helper: cc304b55ff0b34dacbcc0297c093e2e34b70a535; inspect bridge: aac39fcb5ee87288a1d09f03509b58f7a32e0806; web validation/types: 1e01ee58358a884c18c511ec319528feb12db160, f376d2a7364aeb813851bab725f7589d9b50de4d; owner panel wiring: a91131b763d9137761d448877635e295bec51bf8; fixtures: 557cd0126ea2f88effb7e10e264df51eb50bd282, 34375b9a4519717c5005d81ce5a15e931b5f99f2.

- Optional routeQuery on existing studio.buildingInspect, with start/goal room IDs and independent allowStairs/allowElevators booleans. Calls canonical planBuildingRoomRoute with requireAvailableLift=false. Returns ordered native edge IDs and oriented endpoints. Existing requests unchanged. No new solver, file writes, or genre dependency.
- Live owner candidate panel sends the exact stored recipe; imported inspection remains offline. Browser validates query echo, room identities, edge identity/direction, policy, simple path, continuity and destination; route results/errors are associated with the topology and query, and path nodes are highlighted on the all-floors view. Component diagnostics remain explicitly stairs-allowed.
- Added web and native fixtures covering reversed route, excluded elevators/stairs, same-room and unreachable paths, invalid endpoints/steps, and overlapping exterior apertures. Tests authored, NOT RUN. No full native/Studio/Mac/GPU readiness claim.
- PR #136 was observed closed without merge; do not reopen or merge without checking owner integration and branch status. Contents API writes succeeded after Git tree API rejected the prior base tree. Main untouched.

Next: exterior socket projection as explicit non-routing graph nodes; exact-head native and web validation; reconcile shared graph sources with owner/common branches and review PR state. Avoid treating authored connectivity as physical traversal or live elevator availability.
