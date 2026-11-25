# OW-01 mixed group acceptance

Base f0b46a0ca. Add native parking admission to the shared vehicle group owner. Exercise the existing guarded queue with VehicleDrive, VehicleOccupant, a damaged BreakableInstance and arbitrary foreign links. Check fresh cold restore, stable identities, transferred ownership and exact state after wake.

Implementation ef08c32ce. engine/world/VehicleRegionTransfer.h consults the canonical drive, seat resolver, reverse occupant components and transient request queues. Lockstep owners compose this predicate with their existing readiness and pin policy. Native test source tests/world/vehicle_region_group_test.cpp compiled standalone with clang++ -std=c++20 -I. -Ilibraries/include and passed on Mac/KINGSTON, including foreign passenger, pending request on each side and missing slot tests. The 100-transition native GameCore scenario in tests/world/region_gamecore_integration_test.cpp passed clang syntax checking. Execution and merged-tree gate pending.
