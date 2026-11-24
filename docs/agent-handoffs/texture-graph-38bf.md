# Texture Graph Stage 38BF — bounded custom compute nodes

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `9732de9c3c6e45e52f9f350e708e3e6b37bfe9bf`. Previous handoff: 3ba890829f7af67cb769b6487bbe17f3fc52c714. No main merge.

## Implementation
- `engine/render/graph/TextureGraphCustomCompute.h`: validated postfix instruction parser, CPU stack interpreter and GPU expression emitter. No arbitrary shader text, loops, pointer access, external resources or I/O. Four scalar parameters, pixel-center UV, scalar inputs a/b/c or RGB channels ar/ag/ab/br/bg/bb plus c. Arithmetic, guarded division/sqrt, min/max, abs/neg/invert/saturate, fract, sin/cos, step and mix. Decimal literals and intermediate values bounded to [-16,16]; final graph pixels clamp [0,1]. 48 instructions and 1024 bytes per expression; graph-wide 64 Mi pixel-instruction budget per evaluation.
- `engine/render/graph/TextureGraph.h`: customScalar (three scalar inputs), customColor (two color inputs and one scalar input; red/green/blue expressions). Parsed once per compiled node. Existing parameter overrides, reusable functions, CPU evaluator and native graph validation retain ownership.
- `engine/render/graph/TextureGraphGpu.h`: generated expressions for materialized Metal/Vulkan kernels and pointwise fusion; identical parsed instructions, bounded generated code, no untrusted source injection. Custom nodes request materialized execution to avoid repeated terminal re-evaluation.
- Studio inspector expression editor, supported token reference, Mix / UV pattern / Gain-bias examples. Existing graph catalog, property schema, save/load and MCP validation routes reused. Existing texture RHI skill updated.
- Authored parser/interpreter/negative-literal and invalid-code regressions in texture_graph_kernel_source.cpp, and strict scalar/RGB real GPU parity fixture in texture_graph_gpu_acceptance.h. Manifest updated.

## Verification
GitHub accepted 9ff8a358 and 9732de9c. Compared exact committed paths and inspected source excerpts. No C++ build, TypeScript/Vite, CPU fixture execution, actual Metal/Vulkan or visual Studio verification performed. No hardware or performance claim. Main synchronization/merged-tree Mac gate NOT RUN.

## Remaining
Custom Compute is a restricted pointwise scalar/RGB language, not arbitrary HLSL/GLSL/MSL or custom dispatch, custom resource binding, neighborhood sampling or user-defined loops. Backend numeric parity (especially trigonometric boundaries) remains unverified. Earlier 38AZ–38BE also have explicit limitations; read their handoffs. Stage 38BG: consolidated build, all existing targeted CPU and real GPU tests, TypeScript/Vite, live Studio/MCP and visual PBR/HDRI captures; correct errors before considering integration. Keep branch separate from main.
