# PR #124 forest authoring pick alignment

Date 2026-11-24. Branch `fix/pr124-arm64-acceptance`; implementation `fc49c55c2`.

The Studio forest marker occlusion and ground placement now use `visiblePlanetTerrain()`, the same rendered mesh used by general planetary entity picking. This prevents placement and marker selection from relying on the original planet mesh while a live terrain mesh is displayed. Changed path: `apps/studio-native/StudioScene.cpp`.

Evidence: Mac `aiverse_studio` build PASS, `aiverse_studio_model_picking_tests` build PASS, CTest `studio_model_picking` 1/1 PASS, `git diff --check` PASS. These tests cover the rendered-terrain hit API; an interactive eroded-forest placement capture was NOT RUN. Full CI run 45480686461 started on earlier code SHA `fbaa74c25`; it does not include this change. Merged-tree Mac quick gate, full population quality, general live terrain picking acceptance, and main integration remain pending.

Next: finish full CI, run merged-tree gate against latest branch, then review remaining functional World Processes gaps before parent PR integration.
