# W1.32 — race region fixture native compile repair

Owner branch `codex/open-world-acceptance-20261124`; code SHA `d4187d532`. Previous stage handoff: `open-world-stage31-native-fixture-compile-2026-11-24.md`.

Second Mac quick gate on PR #135 integration sources stopped at 45%: `tests/world/race_region_respawn_gate_test.cpp` called `aiverse::lib::mechanics::vehicleHash` without including its owner `engine/mechanics/VehiclePersistence.h`. This stage adds that direct include. Runtime behavior, persistence schemas and CMake linkage are unchanged.

Evidence: `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick` FAIL at build; log `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-183031-89557/build.log`. On Mac AppleClang 17, `cmake --build /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --target aiverse_race_region_respawn_gate_test --parallel 4` PASS. `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --output-on-failure -R '^race_region_respawn_gate_test$'` PASS, 1/1. Full quick gate and other W1 acceptance remain NOT RUN after this edit.

Feature checklist for this bounded repair: implementation, existing owner/API use, dependency and targeted test PASS; all broader native/runtime, persistence, MCP, Studio, Inspector, graph, performance, platform, headless, examples and publishing claims remain unverified or not applicable to the include repair. Next: resume full quick gate and fix any further errors. Keep #135 Draft.
