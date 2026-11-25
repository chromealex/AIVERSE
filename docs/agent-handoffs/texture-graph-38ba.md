# Texture Graph Stage 38BA — native function lowering

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `cc25e25691255eb970bee4d4e182b0af4f33de55`. Preceding handoff: `d78ffa98403b52d0896ee3be1c9694799390db38`. No main merge.

## Source
- `engine/render/graph/TextureGraph.h`: optional embedded functions map; definitions contain typed formal inputs, nodes, named outputs and optional standard parameter declarations. A function call node specifies properties.function, properties.output and optional properties.arguments. Native compile expands each call to independent canonical nodes with collision-free internal names and preserves selected result under the public call ID. Explicit formal kind/arity checks, unknown local reference and duplicate ID refusal; ordinary compiler checks final edge types, cycles, pixel-node work and byte caps. Up to 16 definitions and three inputs; one selected output per call. Nested calls rejected. No new GPU kernel or execution owner.
- `tests/render/texture_graph_gpu_acceptance.h`: CPU pixel assertions for two independent calls, invalid input type rejection and strict GPU bake-set parity fixture.
- Updated existing graph-framework feature manifest; roadmap remains in progress.

## Verification
GitHub confirms implementation commit and three changed paths; reviewed exact committed source and diff. No compilation, native test execution, TypeScript/Vite build or live GPU verification performed. No claim of hardware or feature acceptance. Main synchronization and merged-tree Mac gate not performed.

## Next
Expose function definition authoring, typed call ports and parameter values in Studio, with existing native save/load/validation operations; validate function definitions/instance edits and clipboard/duplicate semantics before declaring editor support. Embedded definitions are persistent snapshots, NOT external linked function assets or automatic updates, nested functions or simultaneous multi-output call nodes. HDRI/IBL, full-size imported texture sampling and custom compute remain open. Consolidate Mac/Vulkan acceptance after feature work.
