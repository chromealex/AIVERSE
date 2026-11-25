# Stage 144 — expanded dense static SDF

Owner: SpectraFX continuation agent. Branch: feature/spectrafx-html5-data-interfaces-20261123
Previous handoff: 2a7b7e6e3522eab8fda99b651984cd30f1115d93
Plan/delivery record: d2c90b0b81ab0b39a9ef603299bf76c360826c51
Implementation: 724b2c9ce80ea915f6b0e0e094b23c73e2eb90dd

Native StaticSdfVolume and FxEffect JSON accept 3..32 nodes per axis; generic MeshDistanceGrid, SpectraSdfBaker, CLI and Workspace fx_graph_edit bakeSdf accept 4..32. Compile-time assertion ties native baker and runtime limits. Studio adds 24 and 32 options, sample count, and triangle budget hint; 16 stays default. Existing graph schema and x-fastest source-buffer ABI unchanged. Metal and Vulkan shader source was inspected: runtime dimension drives address calculations, no fixed 16 cap. Existing GPU allocation initialization and resizing already account for actual SDF payload. No second solver, no new registry, no ECS authority change.

Paths: engine/bake/MeshDistanceGrid.h; engine/fx/{SpectraStaticSdfVolume.h,SpectraSdfBaker.h,FxEffect.cpp}; tools/spectrafx/sdf_bake.cpp; apps/editor/core/SpectraGraphOps.cpp; apps/studio-web/src/panels/SpectraGraphAssetInspector.tsx; tests/{vfx_graph_runtime_test.cpp,vfx_graph_ops_test.cpp}; owning roadmap, agent and skill docs and Studio AGENTS.

Compatibility: previous 3..16 data stays valid; older binaries reject dimensions above 16. Dense 32 cubed is 32768 float samples = 128 KiB, plus five words for GPU metadata. The 16 Mi sample-triangle pair work cap remains, so 32 cubed accepts at most 512 triangles. Existing project path guards, 64 MiB dependency read budget and explicit save semantics unchanged. The existing 1 MiB graph file cap is not increased; multiple dense volumes or complex graphs may exceed it. No compression, external file format, atlas, scalable mesh acceleration, dynamic scene SDF or viewport visualization in this stage.

## Feature delivery checklist

| Criterion | Evidence/status |
|---|---|
| Implementation / architecture | Source committed; existing native owner and GPU ABI reused; functional verification NOT RUN. |
| Native API / headless | Existing C++ API and CLI extended; independent consumer/build NOT RUN. |
| Skill / docs | Owning skill and roadmap updated; skill audit NOT RUN. |
| MCP / Studio / Inspector | Existing shared operation/schema and inspector extended; built registry, bridge, interactive authoring NOT RUN. |
| Persistence / Graph | Existing JSON unchanged structurally; roundtrip and rejection regression source; execution and save/load NOT RUN. No new authoritative simulation persistence. |
| Performance / platforms | Bounded payload and bake work; actual memory/timing, Metal/Vulkan and CPU validation NOT RUN. |
| Dependencies | No new target/dependency; build and link NOT RUN. |
| Examples / tests | Existing registered runtime and graph-ops fixtures expanded for 32 cubed, invalid 33, malformed sample count, work cap and in-memory edit. CTest NOT RUN. |
| Handoff | This separate committed report. |
| Publishing | Real screenshots, devlog and web demo NOT RUN; pending functional verification. |

Actual evidence: GitHub returned successful non-force expected-head update; implementation commit diff read back. No compile, CTest, TypeScript, CLI execution, visual, GPU, benchmarks or CI workflow verification performed (features-first, QA deferred). No main sync or merge: comparison before implementation showed main two commits ahead from merge base and branch 738 commits ahead; main-only paths were AGENTS.md, workflow-parallel contract and two site indexes. Main integration and merged-tree Mac gate remain pending. No claims of readiness.

Next: introduce a bounded external compact SDF asset format, validated resource resolution and shared GPU atlas/storage, then scalable offline mesh acceleration. Maintain existing inline compatibility and explicit save/rollback; address the graph-size cap through external assets, not an unbounded transport increase.
