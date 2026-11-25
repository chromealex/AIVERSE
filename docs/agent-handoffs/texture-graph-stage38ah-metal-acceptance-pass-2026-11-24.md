# Stage 38AH — strict Metal hardware acceptance PASS

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Tested checkout: `36cdc094641d42b5bc04fb6b4640456895daa6c4`

Stage 38AH hardware rerun PASS: user-supplied texture-graph-metal(1).log explicitly checks out 36cdc0946; on Metal Apple M3 Ultra the strict single CTest fixture texture_graph_metal_gpu passed in 2.15 s (2.18 s total), 1/1. Aggregate profile available=true, 50 accepted, 0 rejected, 0 invalid, 0 pending: 10 fused-dispatch, 32 materialized-dispatch and 8 texture-copy samples. Detailed profile available=true, 6 accepted, 0 rejected, 0 invalid, 0 pending: 3 materialized-group (including a fused group) and 3 output-pack samples. Fixture final PASS covers parity, shared scratch, multi-block readback, cancellation, timeout, recovery and native fence-only resident lifecycle. Previous 30/31 suite passed all CPU/native non-Metal fixtures on earlier checkout; combined 31/31 on one post-fix checkout remains NOT RUN. No Vulkan hardware/live Studio/MCP/merged-tree quick gate claim.

## Source evidence

User-uploaded texture-graph-metal(1).log: HEAD line 5; target built; CTest command invokes aiverse_texture_graph_metal_gpu_test from /Volumes/KINGSTON/AIVERSE-builds/texture-graph-stage38-arm64. The aggregate and detail JSON reports have 56 total completed samples with no invalid/rejected/pending samples. Final CTest output reports 100% (1/1), no failed tests. The region report contains groupMembers ["a","i"], demonstrating a fused group, not individual per-node timings.

This records user hardware evidence, not a local assistant-run build. It does not replace clean-checkout test_reports or certify full feature readiness. No default-on reclamation or slot reuse policy change.

## Next

Rerun consolidated 31-test runner on one exact checkout, retaining complete build/test output and commit. Then material_batches --unit, live Client/Editor/Studio/MCP and merged-tree Mac quick gate; Vulkan hardware acceptance separately. Do not merge before required gates.
