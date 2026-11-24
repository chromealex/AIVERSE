# Stage 148 — accelerated mesh SDF baking

Owner: SpectraFX continuation agent. Branch: feature/spectrafx-html5-data-interfaces-20261123
Previous HEAD: 09b2886e416c67cf79651d1b6faa581399cc59dd
Plan: 2457952023323c3465bed25498ab7708ebb9762c
Implementation: 8063e70c4a56ebccc6c3a63aa6b5d4db3a0ceef7
Native stats, fixtures and docs: 23eba8abe162f8fc4f3767b7491f7cf77b403d20

MeshDistanceQueries.h extracts the existing double precision triangle distance and solid angle routines and provides a balanced median-split BVH for nearest-point and oriented winding queries. Existing engine/bake/Bvh.h/.cpp is a float ray-only light-baker interface in an optional linked target, so it was not modified; no additional runtime/renderer library dependency. AABB nearest pruning and oriented positive-axis crossings preserve nonzero winding for closed oriented surfaces. Ambiguous edge/vertex/plane intersections retry alternate axes; when all are ambiguous, fall back to the original exhaustive solid-angle winding. The accelerated path does not perform the old all-point solid-angle numerical consistency check; neither approach certifies mesh self-intersection or nonmanifold vertices.

MeshDistanceGrid preserves exact position weld, oriented edge validation, signed x-fastest grid and atomic output. Removes the old static product limit of 16 Mi sample-triangle pairs. New bounds: 32768 triangles, 98304 input vertices and 64 Mi measured node/triangle operations; complex/pathological meshes may still exceed the actual work cap and fail safely. Optional MeshDistanceQueryStats records distance, node, ray, fallback, retry counts; generic caller may reduce work limit, stats returned on success or failure. SpectraSdfBaker forwards optional stats and existing API calls remain source-compatible. Studio hint reflects actual caps. No change to dimensions (4..32), SDF binary, JSON, GPU ABI, authoring operation, or ECS authority.

Paths: engine/bake/{MeshDistanceQueries.h,MeshDistanceGrid.h}; engine/fx/SpectraSdfBaker.h; apps/studio-web/src/panels/SpectraGraphAssetInspector.tsx; tests/vfx_graph_runtime_test.cpp; docs/spectrafx-modernization-roadmap-2026-11-23.md; docs/agents/spectrafx.md; skills/visuals/aiverse-spectrafx/SKILL.md; apps/studio-web/AGENTS.md.

Regression source: 11 cubed nested reversed shell compares every sample against exhaustive original distance and winding, positive cavity; 32 cubed 768-triangle disjoint mesh above old 512 limit; counted query work versus exhaustive triangle tests; deliberately tiny work cap rejects atomically; previous cube, open mesh, winding and dimension fixtures retained. These are unexecuted assertions, not measured acceleration. No CMake, CTest, CLI, TypeScript, hardware, benchmark, CI or UI verification. Source commits and expected-head non-force updates succeeded; new headers inspected. No main sync/merge. Feature readiness NOT established. Native API and docs source available; no new persistence format or editor operations needed. Runtime asset, packaging, replay, fence and actual platform acceptance still open.

Next: Stage 149 high-resolution external SDF (64/128), revisit allocation, serialized binary size, inline JSON caps, shader indexing and transport limits before enabling dimensions. Stage 150 packed heterogeneous atlas remains open; previous Stage 146 and 147 limitations persist.
