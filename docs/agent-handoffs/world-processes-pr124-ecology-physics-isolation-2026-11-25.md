# PR #124 ecology helper isolation

Date 2026-11-25. Branch `fix/pr124-arm64-acceptance`; implementation `675cb6730`.

Moved the ECS forest climate advance helper into `examples/world/world_processes_native/EvolveTerraEcologyBridge.h`, reused it from `WorldProcessGameplayHost.h`, and changed `examples/world/terraweave_native/Integration.h` to include the bridge rather than the Box3D-bearing gameplay facade. No ECS authority, save format or climate projection policy changed.

Merged-tree `aiverse_terraweave_native_micro_compile_test` build PASS; CTest `terraweave_native_micro_compile` 1/1 PASS. The separate physics smoke target was not configured in the quick-gate build and was NOT RUN here. Full merged-tree Mac quick gate on `d21d0efb8` is still in progress; no PASS claim. Linux ARM64 CI 45480686461 on earlier SHA failed in `apps/editor/windows/AttachmentAuthoringWindow.cpp:589` on GCC `-Werror=missing-field-initializers`; the same line exists in `origin/main` and is an inherited failure. Next: finish Mac gate, fix or hand off inherited GCC warning, rerun CI on current code.
