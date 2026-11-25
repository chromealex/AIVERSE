# PR #124 merged-tree Mac gate

Date 2026-11-24. Branch `fix/pr124-arm64-acceptance`.

The first merged-tree test branch combined `origin/main` at `73a749b64` with the PR #124 head at `00b6b3d48` as a squash, commit `3820b24a4`. `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree` configured successfully and failed during build at 29%: `region_entity_ledger_test.cpp` reused `changes` for two different objects, and `region_ecs_bridge_test` did not link `aiverse_world` for `SpatialWorld::cell`. Evidence: `/Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree/test-reports/20261124-203245-89624/build.log`.

Implementation `2a0681f5f` changed `CMakeLists.txt` and `tests/world/region_entity_ledger_test.cpp`. The repeat merged-tree commit `21eeb6b40` configured and passed the earlier failures, including linking `aiverse_region_ecs_bridge_test`. It failed at 42% in `tests/world/race_region_respawn_gate_test.cpp`: missing direct declaration of `lm::vehicleHash`. Evidence: `/Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree/test-reports/20261124-204020-6798/build.log`. The next stage must include the owning `VehiclePersistence.h`, rerun the gate, and handle any further failures. No quick-gate pass or main integration claim.
