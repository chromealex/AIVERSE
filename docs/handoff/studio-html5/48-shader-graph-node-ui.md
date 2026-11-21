# AIVERSE HTML5 Studio — Stage 48: catalog-based Shader Graph node UI

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 4758a4633ec63e2b39c49d2832839bdebdb61626
Commit: see branch history.

## Implemented
- Connected Stage 47 studio.shaderGraphCatalog to the real HTML5 ShaderGraphPanel. The operation picker shows only native engine operation IDs valid for the graph's current domain.
- UI can add an operation as a new node to the local draft, select a node, and remove it only when no connections/outputs reference it.
- Selected node is visibly highlighted on the SVG canvas. All edits remain local until Validate + explicit Save (Stage 46 compiler-backed operations).
- Switching to another graph while unsaved node layout/structure changes exist now requires Discard & open or Cancel, preventing accidental draft loss.
- Loading/empty/error states and safe mock catalog retained; no arbitrary JS shader code or project filesystem paths introduced.

## Files
apps/studio-web/src/panels/ShaderGraphPanel.tsx
apps/studio-web/src/panels/ShaderGraphPanel.css
docs/handoff/studio-html5/48-shader-graph-node-ui.md

## Validation
- Native/React changes committed to GitHub. No full npm/Vite application build or macOS binary run.
- The Stage 46 pure TypeScript graph-layout helper previously passed 5/5 local Node 22 tests and strict tsc. New Stage 47 operation add/remove tests authored, need rerun.
- GitHub Actions was queued/pending on the last check; no PASS claimed.

## Remaining
- Node ports cannot yet be rewired from UI, constants cannot be edited, and full Shader Graph GPU preview has not been ported.
- ShaderGraphSave immediately writes the project file; MapDoc Undo does not apply. This is disclosed in UI.
- Renderer3D PBR sphere from Stage 44 needs macOS visual verification, and Shader Graph rendering is not bound to that sphere yet.
- Next: execute local Node graph tests, check source parser, then implement link editing and per-graph Undo with native validation and same handoff discipline.
