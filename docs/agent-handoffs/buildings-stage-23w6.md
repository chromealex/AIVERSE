# Stage 23W.6 — integration completion and acceptance wiring
Parent 23b4223a13e15470f2dc44a214aae058042659cb. Target PR #114.
- Corrected Fixed vector division in PHYSIS headers, aligned managed lift count with existing replica protocol, checked monotonic per-lift replica revision and stop motor on reached waypoint.
- Simulation::stepWithInput gates before input injection; RollbackSession uses one readiness check and a bounded failed-restore path.
- Native example adds client-only recipe/model preparation and committed remote pose readback, real fixed-point in-flight checkpoint/resimulation with checksum equality and existing RollbackSession late input, optional real Box3D body mirror/occupancy/rider synchronization. CTest registration for lockstep and optional PHYSIS.
- All four remaining integration paths are present in source; no local compile/test/GPU result. The host still supplies actual movement, admission and external graph policies, and fixed-point lockstep does not include arbitrary Box3D float physics or dynamic scene streaming. Cross-platform determinism and GPU/AI acceptance are not proven. Do not merge until verified.
- Continue with exact-head compilation, focused GLB/PBR, native tests and GPU evidence. No legacy genre module changes.
