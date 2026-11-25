# Stage 38B — shared native Texture Graph owner

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Roadmap/manifest: `9d5264a47fd9f5027e11a227f00ef0eee1ff3d9a`
Implementation: `07dad0a5d8c611d7a09fb781d906df615341becc`
Acceptance fixture: `930a87cea0fab19adab07b26eec97ddd3820cc89`

Stage 38B: canonical request lifecycle is now in `engine/render/graph/TextureGraphGpuOwner.h` (`aiverse::render::texture_graph::TextureGraphGpuOwner`, CMake `AIVERSE::texture_graph_gpu`). It is noncopyable/nonmovable, with typed `ResidentMaterialOptions` and `startResidentMaterial(graph,overrides,scope,lease,options)`; defaults CPU-reference acceptance, optional explicit FenceOnly, mipmaps, fusion, timeout. Same existing JSON start/status/cancel/tick and appearance publication methods; editor `TextureGraphGpuPreview` derives from it and supplies only synchronous asset publication sinks. No Workspace or filesystem dependency in native owner. Host creates exactly one owner per device/scope, supplies its real owner-thread lease, drives tick inside the native graphics frame before encoders, applies a ready matching material at its existing joined frame boundary, cancels/drains stale work and calls shutdownAfterIdle only after proven healthy device idle before retiring epoch. No automatic game-host integration or new global owner, no persistent format change, no ECS authority. Existing GPU fixture now uses native header and public CMake target without editor core; extended for typed native fence-only request and cancellation. Source boundary checks passed, integrated compile, link, Studio/MCP and real GPU execution NOT RUN.

## Native use

Link `AIVERSE::texture_graph_gpu` to the native C++ target in the source build; host supplies the device/backend. Include `engine/render/graph/TextureGraphGpuOwner.h`. Retain one nonmovable owner alongside the existing graphics lifetime.

```cpp
using namespace aiverse::render::texture_graph;
// owner is a persistent member of the existing presentation host:
TextureGraphGpuOwner owner;
ResidentMaterialOptions opts;
opts.acceptance = ResidentAcceptance::FenceOnly; // optional: no CPU parity
auto request = owner.startResidentMaterial(graph, overrides, scope, lifetime.lease(), opts);
auto id = request.getStr("request");
// Each host frame, before other encoders:
owner.tick(device, scope);
// Once ready && !busy && gpuTextureSetReady for exact id:
// call publishCompletedGpuMaterial or publishCompletedGpuSnapshot at
// the existing joined frame boundary with matching publication identity.
// At shutdown after successful device.waitIdle():
owner.shutdownAfterIdle(device);
```

Do not instantiate the example owner each frame. Publication remains a host responsibility. No GPU wait in normal runtime scheduling.

## Changed paths

- engine/render/graph/TextureGraphGpuOwner.h
- apps/editor/core/TextureGraphGpuPreview.h
- tests/render/texture_graph_gpu_acceptance.h
- CMakeLists.txt

## Actual evidence

Read back all four source files at 930a87cea0fab19adab07b26eec97ddd3820cc89; source assertions (no editor include, adapter inherits and has no tick, native fixture uses owner directly, both CMake test link lines drop editor_core, alias exists, fence-only/lease fixture present) all true. This is static inspection, not compilation. No complete checkout available (container network cannot resolve github.com). Integrated native test/Studio/MCP/GPU/Mac gate NOT RUN. Stage 38A two-file budget fixture independently rerun: exact blob SHA e8c88cd8f78274804823386e3d82a1f05fe3563a and 62790cf12a888841abef41ef9ec58d3741cfb418 verified with git hash-object; GCC 14.2 and Clang 17 with -std=c++20 -Wall -Wextra -Werror both exit 0 with PASS. This is evidence only for unchanged isolated budget policy, not Stage 38B native API.

## Next

Compile the new native target without editor/tools; run real Metal/Vulkan acceptance and integrated editor fixture. Verify native game host wiring at its existing frame boundary, then add request queue/incremental execution without a competing owner. Do not merge main until integration gate. Release 3.1.0 candidate: public native texture graph owner, opt-in resident GPU generation (experimental).
