# Stage 38BI — external raster channel extraction

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `8e01a675f6274334726229b303b74ede56ca5a1c`. No main merge.

- New typed scalar `imageAssetChannel` selects red, green, blue or alpha (default), with the same path/revision validation, CPU nearest sampling, Metal/Vulkan materialized kernels, source snapshot queue and byte accounting.
- Project PNG/JPEG resolver preserves alpha in existing 32-bit packed words. JPEG is opaque. Legacy native RGB resolver contract remains compatible (`hasAlpha=false` means opaque); cache identity distinguishes packed decoder layouts. RGB outputs are unchanged.
- Studio pin/refresh retains channel selection; native dependency reporting includes channel nodes in root/function definitions. Authored CPU and strict GPU acceptance fixture for transparent and partially transparent pixels.
- Source changes only; no native compilation, actual GPU/GUI/MCP run or full acceptance performed. General RGBA graph output, HDR, mip/compressed textures, nested functions, dependent asset updates, full GGX/BRDF LUT/EXR and general custom shader resource ABI remain open.
