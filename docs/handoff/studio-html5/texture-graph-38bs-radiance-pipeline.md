# Stage 38BS — typed radiance computation and CPU EXR publication

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `8a22b4fa63aeec3fdf7e4d2694f27d7d07c0ebd3`. No main merge.

- Add separate radiance kind, HDR source and arithmetic nodes, exposure, blend, explicit normalized tone map. Signed float intermediates bounded to half finite range; existing LDR port contracts unchanged. Nested functions support radiance formals/results. Existing materialized GPU float4 buffers retain HDR values; fixed Reinhard preview quantization matches CPU display preview.
- Raw CPU RGBA32F evaluation API and exclusive CPU HALF EXR publication reuse CameraExr writer. Studio exposes radiance output and export, source pin refresh and dependency tracking. RGBA8 bake rejects raw HDR and mixed sets rather than silently truncating. Existing PBR textures still normalized.
- Authored signed HDR CPU expected-value and strict GPU display parity fixture. No native compile, frontend build, CPU/GPU execution or GUI verification performed. No GPU raw float readback or resident HDR texture publication yet. HDR alpha remains opaque; source decoder remains FP16. Remaining full HDR GPU export, compressed formats, PMREM, global dependency scheduler and general custom shader ABI.
