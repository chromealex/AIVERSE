# Stage 127 — focused native acceptance wiring
Date: 2026-11-24. Base 67ba569f6afe34d3b5099efae7ec54c423f0a4a7. Branch: feature/world-processes-stage31-bidirectional-handles.

Stage 126 left the real planetary host fixture optional and unexecuted. The existing focused workflow only compiles the standalone ECS harness. Add a separate Debug CMake acceptance job on Linux and macOS using the same local script, with physics explicitly enabled, no GUI/client or full engine build. Build nine named targets and require each matching CTest individually so a missing optional test is a failure, not an empty/partial successful regex. Capture SHA, worktree, platform, configure/build logs, per-test output/JUnit and summary under the build directory. Existing ECS/ASan jobs remain unchanged.

Local invocation from repository root:
```sh
bash tests/world_processes_native_acceptance.sh build/world-processes-native 4
```

This is acceptance wiring only, not a reported native PASS. CMake/build/CTest and platform results are pending. After focused acceptance, use the existing unified runner with a separate physics-enabled build cache (or run a fresh full build configured with physics ON); then resolve Studio merged-tree and owner activation dependencies described in Stage 126. GPU rendering, durable FRACTURE, TerraWeave lifecycle, MORPH model binding and real GameCore activation remain open. Do not merge main on the basis of this change.
