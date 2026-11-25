# Texture Graph Stage 38BB — Studio embedded functions

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `5d34d4d61bef6d23e22e2da334999dd157d8b227`. Previous native handoff: 5f0d1389a353ac042036788cc0aad6d7b8cf50e3. No main merge.

## Implemented
- `apps/studio-web/src/graphs/textureGraphAuthoring.ts`: derive an embedded function from selected primitive nodes, with the focused node as result, up to three typed external boundary inputs, copied parameter declarations for selected bindings and deterministic formal names. Originals remain in graph. Create independent function calls for a named output; resolve native port kinds in existing graph view. Guard removal of in-use definitions, expanded node/work/document budgets, and missing definitions.
- `apps/studio-web/src/graphs/TextureGraphPanel.tsx`: define from selection, function library and output selectors, add/remove, typed canvas/inspector ports, JSON argument field per declared parameter, existing Undo and native Save/Validate/Preview routes. Duplicate/paste checks expanded function budget. Clipboard reset on graph replacement/definition removal; flattened copy of a saved graph with call nodes refused rather than dropping definitions.
- `apps/studio-web/tests/graphFramework.test.mjs`: authored selection, parameter, typed port, removal, nested refusal, stale clipboard and resource-budget regressions.
- Existing feature manifest updated; roadmap remains in progress.

## Verification
GitHub accepted source commits d0d36f83 and 5d34d4d6. Compared initial implementation diff against prior HEAD and reviewed relevant source. TypeScript/Vite build, npm test:graphs, native GPU and live Studio/MCP verification NOT RUN. Main synchronization and merged-tree Mac gate NOT RUN.

## Limitations and next
Definitions are persistent inside a saved graph, not independently linked project function assets. Function creation uses selected primitive nodes with one result; existing multi-result definitions can be instantiated one result per call. No nested functions, external update propagation, full separate function-body editing workspace or native asset linkage. Parameters use JSON input with native validation rather than dedicated typed widgets. Next stage 38BC: project-scoped linked assets, explicit revision/update semantics and safe native snapshot resolution. Full-size image source, HDRI/IBL and custom compute remain open.
