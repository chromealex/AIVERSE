# Stage 38R — checked renderer material image resolution (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `0dd03b0379742399f8540ce265a567baebfffdad`
Plan: `40fadac77fefeaa60b37027e63ea12ed97ed258e`
Implementation: `eefa681712f3548329650426734335eaf68cd87f`

Stage 38R: shared Model.h render-time image resolver validates bounds, tombstones and tracked procedural generation by the original material pin's control-block identity, with no additional resource owner or pin allocation per draw. PBR slot mapping is shared with graph publication. Ordinary imported/legacy untracked images retain index semantics; tracked procedural images in naked Shader Graph sampler slots are refused (existing ABI has no sampler pin), using established fallback textures. Renderer3D ordinary PBR and graph binding, alpha coverage, mask provenance, motion/reactive/selection bindings, ICB resource snapshot creation and revalidation, batching equivalence/hash and VT base guard use the shared resolver. LENS replay using Renderer3D inherits the checked sampling path; separate camera snapshot/external/native owner audit remains. Both Renderer3D.cpp and Renderer3DModels.cpp have no remaining direct images[] accesses. ICB table still stores raw frame-scoped handles and needs existing retirement/root checks. Portable gpu_icb_material_contract fixture extended for correct/wrong pin, same index/handle replacement, alpha, imported textures, hash and equivalence; NOT RUN. Fifteen exact-commit static wiring checks passed. No claim of full default-on collection, full consumer migration or GPU acceptance.

## Actual evidence

Read exact committed files via GitHub. Fifteen static assertions returned true: tombstone refusal, control-block comparison, shared channel mapping, checked batch equivalence, checked hash, direct PBR binding, ICB PBR binding, graph samplers, alpha coverage, provenance, three reactive/selection bindings, VT guard, no raw indexing in Renderer3DModels, recycled slot test and ordinary compatibility test. Independently scanned both renderer source files for images[ and found no direct accesses. This is source inspection, not compilation or test execution. No new C++ execution; portable fixture, native integration, strict Metal/Vulkan, Studio/MCP, and Mac gate NOT RUN.

## Next

Stage 38S: inspect remaining Model::images raw consumers outside Renderer3D (material overrides, texture cache/streaming, GI, external/native and camera snapshot paths). Ensure stale pins are cleared on imported texture overrides and original pins retained on copy; do not silently manufacture a new generation from an index. Add actual Model image upload/reclamation/tombstone/reuse integration fixture. Review naked graph sampler ABI if procedural graph samplers need supported generation bindings. Keep default automatic collection OFF pending complete root and lifetime proof. No main merge.
