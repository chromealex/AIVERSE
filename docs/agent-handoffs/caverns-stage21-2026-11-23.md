# TerraWeave Caverns — Stage 21 handoff (2026-11-23)

## Branch and actual commits
- Repository: `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`; not merged to main.
- `3169cf4cdcfe02ebf9038db72c5f977659a61076` — `apps/editor/Editor.h`: CavernsBrushPreview state.
- `aaedeccee8a8d88948e293fd002e8df7e9b77d5b` — `apps/editor/Editor.cpp`: 3D wire-sphere drawn using existing Renderer3D lines.
- `ef851b6cd7a90131e642cc8e2b33232d3c8023db` — `apps/editor/windows/TerrainWindow.cpp`: saved SDF snapshot cache and hover preview producer.
- `72099f1a00381e0903e8f329a600c000f30c4400` — `engine/world/TerraWeaveCavernsDirtyQueue.h`: bounded revision-aware dirty-chunk requests.
- `d4d10b4d3d5b370d27cb31a606ff5e81e287430e` — `tests/terraweave_caverns_dirty_queue_test.cpp`.
- `b20300dbd1587bb72a364d2efe5c0a9db2e196b8` — `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff file itself is a later commit; inspect branch HEAD.

## Architecture and implementation

### Editor overlay and brush hover
- `Editor::cavernsBrushPreview`: visual-only, per-editor-frame state with `visible`, centre in world metres, brush radius in metres, Fill/Carve flag. `drawEditOverlays3D()` now adds three great circles (48 line segments each), colored through `rgba`, without a new render backend or shader.
- TerrainWindow Caverns mouse hover uses the existing `Camera::screenRay` and Stage20 `raycastCaveSurface`. The hit is a world XYZ SDF sample. The visible preview is hidden if no hit/tool is inactive. Shift reverses Fill/Carve color and action.
- SDF snapshot parsing is cached by exact Json document equality with `map.data.extra["terraweaveCaverns"]`; edits and Undo/Redo invalidate the cache. The comparison is still O(stamps), and long SDF ray marching is not accelerated.

### Dirty chunk queue
- `CaveDirtyQueue(limit)` takes 3D `CaveDirtyBounds` and mesh resolution/spacing, obtains globally aligned signed chunk keys through `dirtyCaveChunks`, coalesces revisions and emits stable `CaveDirtyChunk{key,sourceRevision,serial}`.
- `pending(maxCount)` returns a bounded slice, `acknowledge(chunk)` removes only a matching revision+serial. Stale completions cannot remove later changes. Capacity overflow fails before mutation.
- The queue is a planning/notification primitive: it does not itself subscribe to editor operations or touch STRATA/Box3D/NavMesh, and is not an automatic live GPU update. This separation is intentional to preserve owner-thread commit rules.

## Tests actually performed
- GitHub tool acknowledged source, test, skill writes, including their commit SHAs.
- **NOT RUN:** native/full AIVERSE C++ compile, CMake/CTest, editor mouse hover / undo, shader/GPU screenshot, STRATA source adoption, Box3D/SpatialWorld/NavMesh runtime acceptance. The new test is source-only and must not be called passing.
- Suggested first native command in full checkout:
  `c++ -std=c++20 -O2 -I. tests/terraweave_caverns_dirty_queue_test.cpp -o /tmp/aiverse-caverns-dirty && /tmp/aiverse-caverns-dirty`
  Then register CMake targets and run all Caverns stages 1–21 + real editor GUI and GeometryScene.
- CaveVolumeSnapshot and editor headers remain uncompiled in the full engine. Do not merge to main on source-only evidence.

## Gaps and concrete next stage
1. Add a runtime listener/adapter for editor `terraweave_caverns_paint` returned `dirtyBounds` and for saved MapDoc sourceRevision changes (including Undo/Redo). It should retain an immutable versioned `CaveVolumeSnapshot`, feed `CaveDirtyQueue`, schedule Stage18 workers and Stage16 seam audits, and then publish affected GeometryScene/STRATA shards and physics only under safe ownership barriers.
2. Stage17/18 workers need persistent bounded job queue integration and a source-field acceleration structure; avoid parsing all stamp JSON on every hover and full SDF volume scans for each ray step.
3. Add real cursor projection and sphere placement validation with cave walls/ceilings, negative Y, thin-wall picks, modal and selection-lock input rules. The current wire sphere is a preview of the analytic SDF, not a depth-tested mesh outline.
4. Finish GPU-fenced GeometryScene source retirement and all-or-nothing Box3D/SpatialWorld/NavMesh swap. Stage8 publication protocol remains an eligibility gate, not a transaction.
5. Run CMake/CTest and visual acceptance. Fix compile failures before claims of production readiness. Continue a dated handoff and skill append after each stage.
