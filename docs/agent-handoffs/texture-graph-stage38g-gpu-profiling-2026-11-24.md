# Stage 38G — request-attributed GPU scope profiling (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `6b1c21c7370cc15f389cf0a5e5de4b9312a6a52b`
Implementation: `8420d3c2d4c0293a624809ca79a488fe74e30d46`
Fixture and optional capture guard: `3ff73de1da4807af4a80aff68cd1aaf8969b47ae`

Stage 38G (partial): optional TextureGraphGpuOwner::tick capture parameter uses existing graph_gpu_profile::Capture and its host-owned completed scope queue; editor passes its existing capture, native hosts may supply their own capture with a sole timing consumer. Three GPU scope names: texture-graph-fused-dispatch, texture-graph-materialized-dispatch (reachable dirty nodes plus output pack on first output; later packs only), texture-graph-texture-copy (GPU copy and optional mips). Request ID and output name are recorded, adaptive view timing unaffected. CPU validation/readback excluded; timing measures encoded scope, not proof of accepted publication, per-node cost or full request latency. Unsupported or refused scopes yield no samples, never a fabricated GPU time. Studio Native GPU scope profiler and existing graph_gpu_profile MCP show the added metadata. Real-RHI fixture collects completed timestamps, prints evidence; set AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS=1 to require all three scope categories. Actual native build and GPU measurements NOT RUN. Stage 38E audit: CameraModelSnapshot retains copied material vectors, Renderer3D static packet entries retain material snapshots and GPU material resource tickets; joined CPU Model readers alone do not prove these external roots detached. No unsafe automatic reclamation enabled. Existing explicit complete-root API remains available.

## Changed paths
- engine/render/graph/GraphGpuProfile.h
- engine/render/graph/TextureGraphGpuOwner.h
- apps/editor/Editor.cpp
- apps/studio-web/src/graphs/GraphGpuProfilePanel.tsx
- tests/render/texture_graph_gpu_acceptance.h

## Hardware verification

Use the existing native Metal/Vulkan acceptance targets and a configured native build with AIVERSE_BUILD_TESTS=ON and AIVERSE_TEXTURE_GRAPH_GPU_TESTS=ON. On a Metal device supporting scope counters, execute:
```sh
AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS=1 ctest --test-dir <native-build> -R '^texture_graph_metal_gpu$' --output-on-failure -V
```
Run the Vulkan fixture without the strict timing environment flag if scoped counters are unsupported. A successful non-strict test without timing samples does not establish profiling support. Record actual device/backend and scope samples, not estimates.

## Actual evidence

GitHub connector confirmed implementation and fixture commits. Local command `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` exited 128: Could not resolve host: github.com. No native checkout, compile, CTest, Studio build, GPU execution or timing measurements in this stage.

## Remaining root-lifetime work

Audit Renderer3D::invalidateModelCachesFor and outstanding GPU material resource tickets, CameraModelSnapshot copies, persistent View packets and third-party native consumers. Automatic collection needs a proven renderer/host detach or enumerated roots before setting complete=true; Model read leases are not enough. Preserve the current 24 live/pending image limit and existing retirement pool until then. Do not merge main without the required integration and Mac quick gate.
