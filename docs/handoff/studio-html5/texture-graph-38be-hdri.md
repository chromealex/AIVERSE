# Texture Graph Stage 38BE — native HDRI/IBL preview

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `4c1122b381b5089ab851c46acbb9129b314fe1e2`. Previous handoff: 32eca843721620824d5ac3ab64d3fdaab51734e8. No main merge.

## Implementation
- `engine/render/EnvironmentIbl.h`: shared bounded CPU preprocessing of real linear Radiance RGBE 2:1 environment into a 130x330 RGBA16F atlas. Four 128x64 roughness-dependent cosine-power reflection bands plus a cosine-weighted diffuse irradiance band, 48 deterministic samples for filtered bands, longitude wrap and pole/band gutters. Constant HDR energy retained. Not a GGX PMREM or cubemap implementation.
- `engine/render/HdrTexture.h`: caller-specified decoded byte cap preflight (default compatibility unchanged).
- `engine/render/Renderer3D.h`, `Renderer3DModels.cpp`, `ModelShaders.h`: opt-in borrowed atlas on ordinary native model draws; explicit model permutation, slot 13 + U6, GLSL and MSL diffuse irradiance and reflection direction/roughness sampling with existing PBR AB term; unchanged ordinary sky/ground lighting without atlas. Explicit refusal of shader graph, virtual texture, baked/dynamic GI, capture and low-tier conflicts. GPU-authored ICB path disabled with environment; caller owns texture lifetime. Does not implement HDR environment for other render passes or specialized VIVID lobes.
- `apps/studio-native/StudioScene.cpp`: isolated native sample renderer, project-relative HDR assets with traversal/symlink refusal, 32 MiB encoded/64 MiB decoded cap, retain existing atlas until successful replacement, idle barrier before destruction; directional studio light independent. Real native PBR capture returns environment identity.
- `apps/editor/core/GraphOps.h`: existing native Studio PBR render operation schema includes environment path, yaw, intensity and explicit reload.
- `apps/studio-web/src/graphs/TextureGraphPanel.tsx`: HDRI asset list, rotation/intensity sliders, explicit reload and native capture provenance.
- `tests/render/hdr_texture.cpp`: authored CPU regression for bounded decode, constant radiance preservation, wrap gutters and nonuniform roughness convolution. Feature manifest updated.

## Verification
GitHub accepted implementation commits 6fbe3de0, f0363ee8, 4c1122b3. Reviewed exact diff and slot declarations. C++ compile, frontend build, CPU fixture, Metal/Vulkan shader and real PBR render/capture acceptance NOT RUN. No hardware or visual result claimed. Main synchronization and merged-tree Mac gate not performed.

## Limits and next
This is actual HDR image-based illumination and filtered reflections in source, with a bounded cosine-power approximation rather than a full GGX prefiltered environment and BRDF LUT. No EXR, HDR sky backdrop, automatic environment import, scene-wide IBL integration, graph/VT/GI coexistence, or specialized hair/eye IBL. Synchronous first-load convolution and idle barrier affect preview latency; no performance measurement. Native model shader variant needs actual hardware acceptance before declaring ready. Next 38BF: bounded custom compute nodes with safe explicit resource contract; then consolidated build, native GPU and visual QA. Keep branch separate from main.
