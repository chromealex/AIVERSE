# PR #124 race gate compile fix

Date 2026-11-24. Branch `fix/pr124-arm64-acceptance`; implementation `f5a40aa46` changed `tests/world/race_region_respawn_gate_test.cpp` to include the owning `engine/mechanics/VehiclePersistence.h` for `vehicleHash`.

The third merged-tree Mac quick gate, commit `12ce45cc0`, passed the earlier ledger, bridge and race compile failures, then stopped at 79%: `terraweave_native_micro_compile_test.cpp` transitively includes Box3D through the newly added `WorldProcessGameplayHost.h` from `examples/world/terraweave_native/Integration.h`. That test intentionally has no physics dependency. Log: `/Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree/test-reports/20261124-204605-16570/build.log`. CI run 45480686461 is still building Linux ARM64 on an earlier code SHA. Full gate remains FAIL; next stage should separate the ecology helper from the physics-bearing gameplay facade and rerun.
