# Texture Graph Stage 38AZ — saved parameterized graph insertion

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `4eb12553d0cb8d50043bc57c8eb2bf14eedfd1df` (base 38AY a969a14e7001e3c2fa5a98c68d47ef970d5aebd1). No main merge.

## Implemented
- `apps/studio-web/src/graphs/textureGraphAuthoring.ts`: deterministic independent instance IDs, internal input and output map, deep-copied properties and remapped exposed parameter bindings; current target channels and parameter declarations preserved. Reject malformed references, duplicate IDs, 64-node / 32-parameter / native pixel-node and document budget excess.
- `apps/studio-web/src/graphs/TextureGraphPanel.tsx`: saved graph selector and Insert saved graph uses existing project-scoped native texture_graph_list/get, single undo edit and selection; status reports remapped output IDs. Reset clipboard, selection and reusable graph selector on project change.
- `apps/studio-web/tests/graphFramework.test.mjs`: independent instances, parameter remapping, no input/source mutation, dangling references and node/pixel budget refusal regression cases.
- Roadmap and graph-framework manifest updated. Native CPU/GPU execution, persistence format and MCP API unchanged; the expanded document is a normal Texture Graph v1 draft, saved by the existing explicit Save action.

## Evidence
GitHub accepted the implementation commits 0337bf21 and 4eb12553; reviewed committed diff via compare. TypeScript/Vite build, npm test:graphs, native Mac and Vulkan, Studio visual/live MCP tests NOT RUN. No prior acceptance result certifies this stage. Main synchronization/merged-tree gate not performed.

## Remaining
This is flattened insertion of persistent parameterized graphs, NOT linked function assets, callable inputs, nested function execution, update propagation or an independent native function opcode. Next implement explicit reusable function interface and linkage with bounded expansion in the canonical native compiler, then full-size imported image sampling and real HDRI/IBL. User compute code requires separately designed validation, resource admission and backend parity. Consolidate build and hardware tests before integration.
