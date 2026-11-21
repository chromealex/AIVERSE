# Lighting Stage 21 — actual Luma Metal HDR

2026-11-20, integration/lighting-main-stage1-20261120.
Code 966963f676658ba3dc75c3ef0760e6c2eb7741a2.

Added Luma screen direct/denoise/GGX runtime, bounded quality policy, and optional
Renderer3D material/normal receiver hooks. New header APIs are additive; no save
format/default changes. Capture passes suppress profiling/ICB/debug output, use
PBR/graph material evaluation and independent depth. Legacy direct ownership
conflicts prevent composition. Fences cover compute and graphics texture readers.
Validated BVH before upload and checked actual dispatch encoding. HDR composition
uses premultiplied RGB/zero alpha, because generic Additive uses source alpha.

Build aiverse_hdr_scene/aiverse_hdr_gl on KINGSTON: PASS. Initial compilation
failed missing GiGpuBvhMetalShader, nested default initialization and assigning
C arrays; fixed. First actual pixel run found black output from alpha-zero
Additive: fixed and retested. Stage21 logs under KINGSTON build logs directory.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma|graph_gi|dynamic_gi|terrain_gi|compute_gi)':
11/11 PASS, report 20261120-150547-51961. Luma nine analytic RGB cases passed:
128 sources/cluster overflow, off, deletion, metallic diffuse suppression, legacy
conflict, dielectric GGX, actual BVH shadow, geometry restoration, metallic GGX.
Existing Metal/GL GI model/terrain/graph acceptance remains PASS. diff --check PASS.
CI/push N/A. Full quick suite/editor/client, arm64 Release and mobile/Vulkan Luma
NOT RUN at this stage. No performance claim. Texture sources and normal maps
reuse the shader path but broad authored scenes need further visual acceptance.

Read docs/luma-native-lighting.md for host contract. Busy changed views can miss
Luma contribution; native host must address this before continuous lighting claims.
Luma host/editor adoption, animation GI, ICB GI, Release and platform acceptance
remain. Next: native Luma host/source ownership, followed by the remaining user
scope. Do not stop after this intermediate stage. All builds on KINGSTON.
