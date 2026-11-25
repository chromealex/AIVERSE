# Stage 38BU — GPU-only resident HDR texture transfer

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `c8faba84287d6d5ce15f342e8333843b0108e779`. No main merge.

- Extend existing Metal/Vulkan buffer-to-texture RHI to explicit RGBA8/16F/32F byte layout, checked row pitch/format matching; float copy single mip. Extend diagnostic readTextureAsync to RGBA32F.
- Reuse the native TextureGraphGpuOwner, materialized DAG planner, result staging allocation, GPU copy candidate retirement and submission fence. Native residentRadianceFormat selects 16F/32F. Half packing is GPU-side; zero host pixel readback or oracle; no CPU fallback. Caller obtains exclusive texture ownership through takeResidentRadiance with matching lease/scope/request after retirement. No automatic material slot assignment or RPC handle exposure.
- Added authored real-GPU destination diagnostic fixture for both supported formats, separate from fence-only production path. No compilation, test execution, hardware verification or timing performed. No float mip generation, Studio resident HDR adoption, HDR mixed export or full PMREM. GPU-to-GPU copy still allocates a destination texture (not aliasing).
