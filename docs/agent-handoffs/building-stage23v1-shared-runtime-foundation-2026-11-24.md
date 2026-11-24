# Stage 23V.1 — shared runtime foundation
Date: 2026-11-24. Repository: chromealex/AIVERSE. Target: feature/aiv20261123_building-grammar / PR #114. Parent candidate: 73bf7c47c217b4a8cfdfbcae7b8a26ebd0103bf7.

- Simulation offers opt-in reserved shared command handlers, post-setup hooks after PHYSIS and persistent ECS schema sealing, and pre-step readiness gates. Non-opted-in projects keep their existing module dispatch and tick behavior.
- BuildingEcsSceneCollection stores per-scene state and content identity under one ECS resource; legacy single-scene API is retained. Managed ECS runtimes can be driven by one common registry and rebound at a structural barrier, not one scheduler per building.
- Extracted pure authored aperture pose calculation from the single-slab owner for the paired-leaf host. Derived nav overlay keeps partial openings logically blocked; stationary partial presentation no longer interpolates to zero.
- Changed files: engine/sim/Simulation.{h,cpp}, engine/buildings/{BuildingEcsRuntime,BuildingEcsReconcile,BuildingApertureOwner,BuildingDynamicNavSource,BuildingAperturePresentation}.h.
- Evidence: exact parent source reviewed and patch transformations checked. No C++ compilation, tests, or GPU execution yet.
- Next: compose these primitives in shared BuildingWorldRuntime with scene leases, actual native graph refresh, authoritative command routing, graphics and recovery. Authoritative cold recovery currently cancels in-flight leaf commands; deterministic lockstep rollback needs separate evidence/design.
