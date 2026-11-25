# PR #124 EVOLVE demo acceptance — 2026-11-24

Owner: World Processes integration. Branch: `fix/pr124-arm64-acceptance`. Code commit: `b215e9c39`, after root-support fix `ff40f3809`.

`aiverse_evolve_forest_demo` includes the native gameplay host, which includes `CircuitDeviceHost.h` and `engine/physics/Scene.h`. Link the demo to the public optional `AIVERSE::physics` target and only define it when physics is enabled; this supplies Box3D headers and runtime without making the default physics-OFF test configuration depend on Box3D. No duplicate simulation owner or new API.

Actual verification on Mac x86_64 / AppleClang 17 / Debug / physics ON: `cmake --build /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native --parallel 4 --target aiverse_evolve_forest_demo` exit **0**. `env TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native python3 tools/evolve-demo-acceptance.py /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native/aiverse_evolve_forest_demo` exit **0**: rollback replay, disk roundtrip, resumed versus uninterrupted tick-2048 checksum and SVG parse PASS. `git diff --check` PASS. The prior nine-test native script passed on `ff40f3809`; it was not rerun after this CMake-only change. Build with physics OFF, Studio, linked MCP, GPU, browser, performance, full suite and merged-tree Mac gate NOT RUN.

Feature gate: implementation/architecture/dependencies/native demo/examples/persistence/headless PASS for this bounded demo stage. Native API unchanged; skill/MCP/Studio/inspector/graphs not changed here. Platforms Mac CPU only. Handoff/documentation present. Publishing and complete PR readiness NOT RUN. Next: verify physics-OFF CMake configuration, Studio/MCP authoring and linked authentication, visual capture, CI and merged-tree Mac gate.
