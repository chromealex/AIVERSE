# Shared Graph Framework — native GPU acceptance — 2026-11-24

Owner: graph framework / editor native GPU owner. Branch: feature/aiv20261124_texture_graph_gpu_acceptance. Base: 246709e81f9b627b7a3359e3ebc8d7e4eaa35de5 (feature/aiv20261123_feature_gate, draft PR #120).
Planning commits: d66822041486dd2c7df69d076abc935431449b9d, dd4204d451155fa5d4e8f2422d13d0eda53a531d.
Implementation HEAD: 8d0dd8bb516e2342b00cc6cf4e6b6d0099069ad7.

Parallel branch deliberately avoids modifying the parent branch while another agent implements Stage 33B DAG lifetime planning. No new runtime ownership, algorithm or graph format. Test calls production TextureGraphGpuPreview::start/tick/status/cancel/shutdownAfterIdle against the real Metal/Vulkan gfx::Device. Fixture is 256x256 gradient height + metallic (intervening pointwise pass) + normal + roughness (duplicate height), requireGpu=true. It asserts per-output backend provenance (owner only publishes completed parity-accepted readback), one accepted shared scratch hit, one duplicate output reuse, one final normal dispatch, four 64 KiB readback blocks, at most 512 KiB retained buffer payload, no unexpected publication, distinct repeated request IDs, stale/queued/submitted cancellation and drain, queued deadline and recovery. Test-only waitIdle between frames; not a new runtime stall. Tests do not simulate device loss or assert speedup. A missing device is failure, not skip/pass; Vulkan driver name is printed, software execution is not physical GPU evidence.

Changed paths: tests/render/texture_graph_gpu_acceptance.h, tests/render/texture_graph_gpu_acceptance_vulkan.cpp, tests/render/texture_graph_gpu_acceptance_metal.mm, CMakeLists.txt, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md, docs/features/graph-framework.json, docs/devlog/systems.json.

Verification performed in this turn: Linux GCC 14.2.0 isolated scratch policy rerun; local files verified with git hash-object against the fetched GitHub blobs at cbf7b0ecb31ab4dddc4f1abb9799f3f1dae0fcb8 (header cc16e2ebfde4c045a0750cb11db02da4f88b64d4; fixture 68db32fa455258b908babb5aef8a8cd2244f6d77). Command: c++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_shared_scratch.cpp -o scratch_test && ./scratch_test. Actual output: PASS: scratch identity, acceptance and invalidation. This rerun independently confirms the isolated 33A policy fixture; it is not integrated evidence or a test of the newly authored fixture. No clean full checkout, no /dev/dri or Vulkan ICD manifest, no glslangValidator, no Mac/Metal. git ls-remote https://github.com/chromealex/AIVERSE.git HEAD failed: Could not resolve host: github.com. The GitHub connector writes succeeded. Native acceptance CMake configure/build/CTest, GPU execution, feature gate, Studio and Mac merged-tree quick gate NOT RUN.

Suggested Mac runner (use the repository-prescribed /Volumes/KINGSTON volume; do not fall back to internal disk):
```sh
cmake -S . -B /Volumes/KINGSTON/aiverse/texture-graph-gpu -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_TEXTURE_GRAPH_GPU_TESTS=ON
cmake --build /Volumes/KINGSTON/aiverse/texture-graph-gpu --target aiverse_texture_graph_metal_gpu_test --parallel
ctest --test-dir /Volumes/KINGSTON/aiverse/texture-graph-gpu -R '^texture_graph_metal_gpu$' --output-on-failure
```
Vulkan: native configured Vulkan build with AIVERSE_VULKAN=ON and the same opt-in, build aiverse_texture_graph_vulkan_gpu_test and run ctest -R '^texture_graph_vulkan_gpu$' --output-on-failure. Record driver and exact SHA, output and provenance. Capture real hardware acceptance separately from software Vulkan. No main merge or release.

Next: compile both fixtures, fix genuine build/driver failures, record per-backend evidence. Reconcile acceptance-only changes with the parent agent's Stage 33B liveness work without replacing their roadmap/manifest/CMake additions. Keep strict feature status not_run until integrated checks pass. Real device loss fault injection and runtime GPU-to-GPU ownership remain separate.
