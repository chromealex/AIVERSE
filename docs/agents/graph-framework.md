# Shared Studio graph authoring foundation

The framework is presentation infrastructure, not a graph runtime or a new persisted format. Native owners remain authoritative: Shader Graph compiler, SpectraFX, MotionWeave, modeling, navigation and gameplay graph systems. No graph evaluation in the WebView.

Stage 1 API:
- src/graphs/graphFramework.ts: GraphView, NodeView, Port, Link, GraphPolicy, GraphAdapter<T>, GraphRegistry<T>, validateTopology, DraftHistory<T>. Adapters project native documents and return immutable drafts. Use explicit cycle policy (state graphs may loop); native domain validation always decides whether a graph can be saved.
- src/graphs/GraphCanvas.tsx: shared layout canvas with pointer capture, inverse SVG coordinates, keyboard selection and movement, semantic Studio tokens. Canvas only emits select/move callbacks. No file writes.
- src/graphs/shaderAdapter.ts: material, postfx and particle domain registry, existing shader projection and movement. Shader panel uses the shared canvas; existing typed bridge and native compiler remain unchanged.

DraftHistory is a local immutable-reference utility, not yet wired into the shader panel or native Undo/Redo. Topology checks are advisory, not a substitute for native arity, schema or compiler checks. No claim of complete node wiring, graph inspector, debugger, profiler or migration. No new runtime library or CMake target.

Next: coordinate with Studio owner before migrating more panels; extract typed node catalog and port interaction, integrate draft history and inspector, then adapter-based native validation/save/preview for SpectraFX, MotionWeave and modeling. For other graph families use the actual owner schema; graph algorithms alone do not imply a node editor.

Tests: apps/studio-web npm run test:graphs, npm run typecheck, npm run build, native Mac visual verification. Existing graph operations are unchanged. See the stage handoff for real results.
