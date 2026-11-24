# Stage 38CA — GPU IBL atlas prefilter

Parent: `6bc06334bf51452d87c77ee60041cb1beeeb2da9`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Metal/GLSL compute produces existing renderer-compatible half-float atlas: GGX roughness bands, cosine irradiance and split-sum BRDF LUT with padded gutters. Direct resident RGBA16F input; no CPU radiance readback. Output via aligned storage-buffer texture copy.
- Studio owner schedules and retains source/pipeline/buffer/candidate through exact covering fence; previous atlas stays until successful publication. Poll/cancel actions, capability gate and GPU UI path. Existing CPU path retained.
- Fixed atlas, not full cubemap mip chain; GPU numerical parity, compilation, driver execution and cancellation/device-loss QA pending. Main unchanged.
