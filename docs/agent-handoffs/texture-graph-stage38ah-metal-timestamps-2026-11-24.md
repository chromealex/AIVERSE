# Stage 38AH — Metal timestamp marker correction

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `842ec1c26fd57cac6d54f0eabe95e8b18775e54a`
Plan: `6a6bb2b9cdaf839477756aac47acf824b7b7f557`
Implementation: `9afdc69c70de6d395118e6396dc7f590d1d4a217`

Stage 38AH: user ARM64 Mac M3 Ultra Release CTest log at acceptance checkpoint reports 30/31 passed (10.62 s); only texture_graph_metal_gpu failed, strict GPU scope evidence missing; 50 accepted, 0 rejected, 50 invalid, no samples. The log does not print SHA, preceding checkout was 842ec1c26fd57cac6d54f0eabe95e8b18775e54a. Likely empty marker blit encoders were elided; hardware root cause not yet confirmed. Added nonempty 256-byte private marker fills for both explicit/stage sampling, measuring after opening marker and before closing marker; bounded invalid timestamp diagnostics report raw samples, completion status, bytes and calibration. No CPU fallback, no weakened strict gate or new wait. 11/11 exact-source assertions passed at 9afdc69c70de6d395118e6396dc7f590d1d4a217; corrected Metal build and hardware rerun NOT RUN.

## Native evidence
User pasted CTest log: all 30 other tests passed, including camera_client_adapter, camera_model_snapshot, deferred_loading, hosted_view, resource owner group and image pins. Metal requests reached ready; aggregate profile had accepted=50, invalid=50, available=false. Strict failure stops later detailed scope validation. Do not interpret this as a complete GPU pass. No exact SHA embedded in log.

## Correction
Metal scope markers previously used empty blit encoders. The fix encodes a real private-buffer fill in each marker pass. Counter sampling retains the existing paired hardware timestamp and clock-calibration method; timestamps exclude the marker fills, but include boundary/scheduling overhead. Diagnostics report at most eight invalid results per device when strict timing or AIVERSE_METAL_SCOPE_DIAGNOSTICS is set. Hardware confirmation required. Apple counter sampling documentation: https://developer.apple.com/documentation/metal/sampling-gpu-data-into-counter-sample-buffers

## Next
Fetch latest HEAD in a dedicated checkout, reconfigure the existing ARM64 build, build aiverse_texture_graph_metal_gpu_test, run CTest -R '^texture_graph_metal_gpu$' with AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS=1 and AIVERSE_METAL_SCOPE_DIAGNOSTICS=1, capture full output (including successful scope reports via -V). If strict test passes, rerun full consolidated 31-test suite. Material batches, live Studio/MCP and merged-tree Mac gate remain open.

## Hardware rerun result

Stage 38AH hardware rerun PASS: user-supplied texture-graph-metal(1).log explicitly checks out 36cdc0946; on Metal Apple M3 Ultra the strict single CTest fixture texture_graph_metal_gpu passed in 2.15 s (2.18 s total), 1/1. Aggregate profile available=true, 50 accepted, 0 rejected, 0 invalid, 0 pending: 10 fused-dispatch, 32 materialized-dispatch and 8 texture-copy samples. Detailed profile available=true, 6 accepted, 0 rejected, 0 invalid, 0 pending: 3 materialized-group (including a fused group) and 3 output-pack samples. Fixture final PASS covers parity, shared scratch, multi-block readback, cancellation, timeout, recovery and native fence-only resident lifecycle. Previous 30/31 suite passed all CPU/native non-Metal fixtures on earlier checkout; combined 31/31 on one post-fix checkout remains NOT RUN. No Vulkan hardware/live Studio/MCP/merged-tree quick gate claim.
