# Stage 38BL — DDS/KTX2 authored mip sources

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `3ae1ab19fcef47a8f5b626098ed20d2ecb8ce986`. No main merge.

- Bounded preflight of 2D DDS/KTX2 before existing container parser allocations; decoded full chain budget and KTX2 expanded payload budget, no arrays/cubemaps/volumes/Basis.
- Existing CPU decoders expand supported BCn, ETC2/EAC, masked DDS and RGBA8 to immutable packed source; original floor-halved mip dimensions/content retained including incomplete chains. LOD clamps to available authored levels; no derived replacement. Source identity distinguishes authored and generated chains. Queue/GPU use same snapshot upload. Base mode still counts entire authored chain.
- Studio picker and native inspection expose formats and mip count; added CPU expected-value and strict GPU authored mip fixture. No compilation or execution performed. ASTC, BC6H, Basis and native compressed GPU sampling remain unsupported in graph; no full HDR output or sRGB-aware filtering yet. Continue nested functions/dependency updates/IBL/custom ABI and remaining source gaps before final QA.
