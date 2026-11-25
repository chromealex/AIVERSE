# Stage 38BV — HDR mip chains and Studio resident adoption

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `9bbb11b2c1b1951aebf895f185cf4810d4938466`. No main merge.

- RHI capability for RGBA16F GPU-generated mip chain (Metal / Vulkan linear blit features), full texture footprint admission, explicit rejection of unsupported formats. RGBA32F remains single mip. Existing owner retains copy/fence lifetime.
- Studio session-owned resident radiance texture, exact request adoption, idle replacement/release and project teardown, optional bounded mip diagnostic readback with Reinhard display. UI format/mip controls and metadata. No CPU readback during generation.
- Authored destination mip-level diagnostic fixture. No compile, hardware, UI or timing verification performed. No automatic binding to model emissive/material slots (existing shader expects sRGB), no conversion to IBL atlas, no RGBA32F mip generation. Remaining compressed formats, HDR alpha, PMREM, global dependencies and custom shader gaps.
