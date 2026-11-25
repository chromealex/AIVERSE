# Stage 38L — detailed GPU region profiling

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `63fdac8a4013883f4c1e0c5a815931483b68fe0f`
Implementation: `b192b60cdd07732649caa7fd2b6f996b935778ae`
Studio/MCP and hardware fixture: `c9a534d37c6f6e69fb86b2a327b100e19a939010`

Stage 38L: optional textureGroupDetail=true when starting existing graph_gpu_profile MCP capture (native Capture::start(info,scope,true), Studio Native GPU scope profiler checkbox). MaterializedSources retains exact authored node membership of each scheduled root; singleton for unfused node, full topological region for pointwise fusion. Detail mode replaces materialized aggregate dispatch scope with texture-graph-materialized-group around each actually dispatched group and texture-graph-output-pack around actual packing. Existing texture-copy and legacy fused-output aggregate scopes remain, no nested timers. Completed positive native RHI timestamps only, tagged with request, triggering output, groupRoot, groupMembers, passOrdinal, slot and logical slotBytes. Group timing is NOT individual fused-node latency or exclusive output cost. Cached/reused groups do not submit timing scopes; unavailable/refused/throttled samples are not fabricated. Limit 12 attempted group scopes per frame plus existing 32 pending/120 sample caps; report textureGroupThrottled. Profiling metadata failure is optional and cannot refuse generation. Device timing queue remains host-owned; no new collector or CPU time masquerading as GPU. Existing Metal scope support is device-dependent, Vulkan scoped timestamps not implemented. Isolated exact-blob metadata/admission test passed GCC/Clang and ASan/UBSan; integrated build, real GPU timing fixture and Studio/MCP live tests NOT RUN. Stage 38J/38K default automatic reclamation and reuse remain gated by concrete external-root proof.

## Actual evidence

Reconstructed exact Git blobs under a temporary Linux directory and checked git hash-object before compiling:
- engine/render/graph/TextureGraphGroupProfile.h: 386a40df830e9e7f8815816b07e97145c66fad2f
- tests/render/texture_graph_group_profile.cpp: 0893b73a727075c931109456a4df72593541ad6e

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_group_profile.cpp -o "test-$cc"
  ./"test-$cc"
done
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/render/texture_graph_group_profile.cpp -o test-sanitized
./test-sanitized
```

GCC 14.2.0 (Debian 14.2.0-19) and Clang 17.0.0: all three executions exited 0, PASS region metadata, duplicate/invalid bounds, per-frame timing admission; no sanitizer diagnostics. This is an isolated CPU policy test. Read back the committed Studio/MCP schema, owner dispatch and fixture instrumentation. No full build, integrated CTest, real GPU timings or Mac gate run. Hardware fixture now uses separate aggregate and detailed capture epochs and checks request identity, fused membership and no nested aggregate. Strict AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS=1 requires completed aggregate and region/pack evidence. Do not report these as measured results before executing.

## Next

Complete stock host raw-root certification for automatic reclamation/recycling; do not merely return true from the provider. Full integrated build and Metal/Vulkan acceptance (strict timing only on supported hardware), Studio/MCP smoke, Mac quick gate. Default reclamation remains disabled; no merge main until verified.
