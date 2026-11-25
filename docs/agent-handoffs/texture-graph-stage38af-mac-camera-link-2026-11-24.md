# Stage 38AF — native camera test linkage correction

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `8f6d1ccbeee342993727463ef09571dd98f0fc82`
Plan: `718a5933e7451094bb0d4b6d09b4953465f7e3ed`
Implementation: `9c0075a3ea3a75ca8fdde317ff63e1c438215efb`

Stage 38AF: user Mac Release build at the preceding acceptance checkpoint failed linking aiverse_camera_model_snapshot_tests (x86_64 undefined Renderer3D::detachCompletedGraphImageReplay and canDetachCompletedGraphImageReplay). Snapshot target intentionally links engine/View/zstd only; Renderer3D.cpp lives in aiverse_runtime. Moved the complete replay callback preflight/refusal fixture to existing aiverse_camera_client_adapter_tests, already linked to runtime with platform aiverseMain. Kept the standalone snapshot fixture and its dependency boundary unchanged. Added camera_client_adapter to consolidated runner (31 tests per backend). 10/10 exact-source checks passed at 9c0075a3ea3a75ca8fdde317ff63e1c438215efb; Mac rebuild and CTest NOT RUN after correction.

## Evidence and next
User log: cmake --build /Volumes/KINGSTON/AIVERSE-builds/texture-graph-stage38 --config Release --parallel 8 --target ... exited 2; camera snapshot linker failed with the two undefined symbols. Not an architecture-specific implementation bug; its target lacks the renderer translation unit. No test removed: all callback capture/packet tap/geometry tap refusal assertions now run in the existing runtime-linked adapter target. No production code or CMake dependencies changed. Source readback confirmed fixture movement and target registration. Native link and integrated tests remain unverified.

Use the same checkout and existing build directory; fetch and switch to the new exact HEAD, rerun the consolidated runner. Preserve logs. Do not delete the build directory or force reset. Separate material_batches --unit, live host and merged-tree Mac gates remain required.
