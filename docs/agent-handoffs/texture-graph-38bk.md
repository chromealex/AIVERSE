# Stage 38BK — source mipmaps and filtering

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `1d6ae0b43aae37d0e69ddae6edb146e47920b008`. No main merge.

- External source properties: nearest/bilinear/trilinear, base/auto/explicit mip selection, fractional mip level and bias. Existing default base nearest remains unchanged.
- Derived contiguous bounded mip chain for packed LDR and linear float HDR; ceil-halved NPOT extents and valid 2x2 box average, clamp-to-edge bilinear and adjacent mip interpolation. CPU and generated Metal/Vulkan sample shared LOD plan. Exposure occurs after HDR filtering. Straight alpha and encoded LDR averaging; no sRGB-aware or premultiplied filtering yet.
- Existing snapshot/queue/GPU byte accounting includes full derived chain; reject over-budget source before reserve. Cache identity versions mip derivation; authored filter/LOD properties and resolution participate in existing graph keys. Studio catalog/inspector exposes controls.
- Authored NPOT fractional-LOD expected-value and strict GPU parity fixture; no compilation, test execution, hardware, GUI or MCP verification performed. Compressed/imported mip chains, anisotropic sampling, full HDR output, nested functions, dependent asset updates, GGX/BRDF and general custom resource ABI remain open.
