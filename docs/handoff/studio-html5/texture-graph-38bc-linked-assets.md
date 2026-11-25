# Texture Graph Stage 38BC — project function assets and pinned revisions

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `c657a449d1e7ed2c5bc590ed22174d4ad02d41a0`. Previous stage: `4cd455dedd8b63759e50bdb9a93240a79f8d5c53`. No main merge.

## Implementation
- `apps/studio-web/src/graphs/textureGraphAuthoring.ts`: standalone function asset carrier is a normal Texture Graph v1 document with a functionAsset version/definition marker, typed neutral preview inputs and named output probe. Each linked local definition is an embedded snapshot; functionLinks maps its alias to safe project asset and exported definition names. Refresh compares ordered formal input IDs and kinds, named result kinds and exposed parameter names/schema types. Compatible refresh replaces definition only, preserving node IDs, wires, argument values and graph outputs. Incompatible changes refuse, detach keeps embedded definition.
- `apps/studio-web/src/graphs/TextureGraphPanel.tsx`: publish new asset, inspect exact previous document, publish revision with native expected-document CAS, link, refresh, detach. Every named result receives a native validation probe before publication or import; refresh also validates entire candidate before applying one undoable edit. Project/document changes clear asset inspection state. No browser-side file writes.
- `apps/editor/core/GraphAssetDependencies.h`: existing graph_asset_dependencies MCP/Workspace read-only route reports literal texture function links and embedded definitions. It does not certify external content or revision.
- `apps/editor/core/MaterialOps.cpp`: save rejects pretty-serialized payloads beyond the existing 256 KiB read limit. Existing native texturegraphs/<safe-name>.json get/list/set remains sole storage owner.
- `apps/studio-web/tests/graphFramework.test.mjs`: authored carrier, independent pinned snapshot, compatible refresh, retained wires/arguments, alias collision, incompatible interface and detach regressions.
- Updated existing feature manifest; roadmap stays in progress.

## Usage
Define a function from selected nodes, choose its definition, enter a separate asset name, Publish new asset. In another graph choose the saved document and Link asset, then Add function. To publish a changed definition with the same exported name, Inspect asset and Publish revision. Consumer graphs select the linked alias and Refresh linked explicitly, then Save. Detach link converts to local-only definition. Existing offline compilation uses only embedded snapshots and never reads external files. Missing sources leave saved snapshots intact.

## Verification
GitHub accepted implementation commits d9681615 and c657a449. Reviewed changed-path comparison and exact storage/dependency source. TypeScript/Vite, npm test:graphs, native build, real Metal/Vulkan, live Studio/MCP and merged-tree Mac acceptance NOT RUN. Main synchronization not performed.

## Limitations
The separately saved assets reuse texture graph document storage, not a new importer or content browser asset format. Refresh is explicit per open graph, not automatic propagation across all saved consumers, filesystem watcher, cross-project linking or content-addressed history. Breaking revisions are refused; missing sources can be detached. No nested functions or simultaneous multi-output call node. Native runtime uses pinned snapshots; native/MCP callers can save/read these documents and inspect links, but the high-level publish/refresh workflow is currently Studio-side. Next: full-resolution imported textures, HDRI/IBL and bounded custom compute; later consolidated build and hardware validation.
