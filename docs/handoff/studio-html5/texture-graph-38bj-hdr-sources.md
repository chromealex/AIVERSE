# Stage 38BJ — bounded linear HDR source snapshots

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `44072a083a61df92af44ab1c97a1f276dcb8ab94`. No main merge.

- Native pinned project resolver accepts Radiance RGBE and portable uncompressed scanline EXR using existing decoders, bounded by remaining float snapshot budget before decode allocation. Existing half-float decoder precision/range is retained; expanded to float32 RGBA for CPU/GPU source sampling.
- Same native typed image nodes, queue, cache, and materialized Metal/Vulkan paths support alternate float source storage. Budget counts actual bytes (16 per HDR texel); source identity distinguishes representations. Existing LDR source behavior unchanged.
- Explicit integer exposure -12..12 EV and Reinhard or clamp mapping at HDR node output; alpha bypasses tone mapping. Negative radiance clips at mapping. Studio asset picker and native inspection report formats. Normalized graph outputs remain RGBA8; arbitrary HDR graph math, full HDR output/export, compressed EXR, mip/compressed textures, nested functions, dependency updates, GGX/BRDF and custom resource ABI remain open.
- Authored CPU expected-value and GPU layout/parity fixture; not executed. No native build, hardware, GUI or MCP verification performed. Decoder transient allocations are additional to immutable snapshot cap.
