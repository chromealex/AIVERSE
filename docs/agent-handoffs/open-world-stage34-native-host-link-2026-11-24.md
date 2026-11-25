# W1.34 — native host registry link blocker

Owner branch `codex/open-world-acceptance-20261124`; code SHA `25220a83a`. Path `engine/game/native/Build.cmake`.

The full Mac quick gate on integration source failed at 79% while linking `aiverse_camera_client_adapter_tests` and `aiverse_vivid_renderer_preview`: the generated `NativeRegistry.cpp` called `game::NativeRegistrar`, whose implementation resides in `aiverse_game`, but `aiverse_native_modules` did not link that owner for runtime-only executables. The existing interface target now links `aiverse_game`. No new registry, game module, feature API or persistence schema was added.

Evidence: full gate FAIL, log `/Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick/test-reports/20261124-183725-660/build.log`. On Mac AppleClang 17, `cmake --build /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --target aiverse_camera_client_adapter_tests aiverse_vivid_renderer_preview --parallel 4` PASS after the change. `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-acceptance-20261124/quick --output-on-failure -R '^camera_client_adapter$'` PASS, 1/1. The VIVID preview is GUI driven and was built, not visually exercised. Focused W1 suite previously passed 11/11 and operator jobs 1/1; full gate after this edit NOT RUN.

Feature checklist for this bounded build repair: implementation, architecture and dependencies PASS by target build; targeted native CTest PASS. Runtime behavior, Studio, MCP, Inspector, persistence, graphs, performance, platforms beyond Mac, GUI preview, headless game and publishing remain unverified or not applicable to this link repair. Next: restart full quick gate and inspect all failures before declaring W1 accepted. Keep #135 Draft.
