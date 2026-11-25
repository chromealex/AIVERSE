# Stage 38M — Vulkan scoped timestamps

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `f1cc3a14785d99d75813466bec299062e0b99391`
Implementation: `312363656b466cf1c39a6eaf508a6e2c174539b7`
RHI fixture: `f18fe958a3c6054db23384cb56d2e184bc65abdb`

Stage 38M: Vulkan now implements Device::beginGpuScope/endGpuScope/takeGpuScopeTimings, separate from AIVERSE_VK_GPU_TIMING logs. Query pools created lazily per frame slot when timestampValidBits and timestampPeriod are valid; 32 scopes (64 queries) per slot, nonnested, outside render/compute encoders. Results consumed only after actual slot fence completion (existing prepareSlot wait or nonblocking vkGetFenceStatus), no VK_QUERY_RESULT_WAIT_BIT and no new wait. Unbalanced/failed scopes report invalid; completed result mailbox bounded to 128. Timestamp differences use valid-bit mask and device period; GPU timeline intervals, not exclusive busy time. Vulkan backend implementation flag now true, actual support remains device dependent. Existing Studio/MCP capture routes unchanged. Real-RHI fixture adds Vulkan lifecycle assertions; strict AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS=1 applies on supported Vulkan as well as Metal. Isolated CPU policy test passed both compilers and sanitizer; integrated build and hardware NOT RUN.

## Actual evidence

Reconstructed exact blobs and verified with git hash-object:
- engine/gfx/vulkan/VkScopeTiming.h: 1adac2e8d8193766bc66ad43684938b61fd739da
- tests/render/vulkan_scope_timing.cpp: 7248c871c2ff3862388626423d85e0781748ded9

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/vulkan_scope_timing.cpp -o "test-$cc"
  ./"test-$cc"
done
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/render/vulkan_scope_timing.cpp -o test-san
./test-san
```

GCC 14.2.0 (Debian 14.2.0-19), Clang 17.0.0: three executions exit 0, PASS scope pairing, bounds, abandonment, timestamp wrap and units; no sanitizer diagnostics. Local git ls-remote failed DNS (exit 128); Vulkan SDK headers absent at /usr/include/vulkan. No full backend compile or real device execution. Fixture asserts lifecycle and strict GPU timestamp evidence but is not measured evidence.

## Next

Complete concrete stock host raw-root/replay proof for default automatic collection and recycling. Run Vulkan validation-layer acceptance, strict timing on supported queues, Metal acceptance, Studio/MCP and Mac quick gate before main.
