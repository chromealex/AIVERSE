# AIVERSE — Stage 131: pooled GPU geometry A/B controls

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage131-pooled-geometry-20261120`  
**Draft PR:** [#77](https://github.com/chromealex/AIVERSE/pull/77), targeting `perf/batching-stage129-vertex-workload-20261120` (Draft PR [#75](https://github.com/chromealex/AIVERSE/pull/75)), which targets `perf/auto-batch-cost-metrics-20261119` (Draft PR #39).  
**Exact branch source base:** `89a0add8097ed8931292367794eba05745719ba9`, includes Stage130 interleaved trial ordering and macOS CI smoke gates.  
**Latest implementation commit before handoff:** `a197a128dd74de0f9dbcbdb24327bde31e5cd728`.

## Problem and supported rationale

Stage129 proposed varying tessellation from 1 to 64 triangles/primitive while holding 2048 original model primitives and actual Metal CPU MDI commands/submissions fixed. That baseline would silently cross the **16 MiB per-model Global Geometry Pool admission limit**:

- `ModelVertex` is exactly 60 bytes (static_assert in `engine/render/Model.h`). There are 3 * N² independent vertices and 3 * N² uint32 indices per original primitive.
- `2048 * 3 * (60+4) = 393216` authored bytes at N=1 (1 triangle/primitive).
- `2048 * 3 * 16 * (60+4) = 6291456` authored bytes at N=4 (16 triangles/primitive).
- `2048 * 3 * 64 * (60+4) = 25165824` authored bytes at N=8 (64 triangles/primitive) **before** geometry preparation.
- `engine/render/GlobalGeometryPool.cpp` rejects any model whose vertex+index upload exceeds 16*1024*1024 bytes; `uploadModel()` can instead fall back to dedicated VB/IB.

Therefore the original N1-vs-N8 comparison could conflate geometry/vertex work with changing GPU allocation type, buffer residency and GPU-state grouping; the unpooled N8 case may not preserve the expected 18 ICB submissions / 4086 commands. **Neither the 16 MiB inequality nor source review establishes that native N4 was admitted to the pool**; this is now a required measured invariant.

## Stage 131 changes

1. `tools/batching-stage129-trials.sh`: change core A/B from N1/N8 to **N1/N4** (1 vs 16 triangles/primitive). Keep Stage130's alternating mesh-order per OFF/ON Direct/MDI cell and two process repeats per cell (16 total independent processes). Update manifest to `mesh_subdivisions=1,4`, `mesh_order=interleaved_alternating_1-4_4-1_per_mode`, and `geometry_pool_required=1` with 16777216-byte admission limit. After every process assert the new `stress diagnostic geometry pooled=1 vertex_bytes=... index_bytes=...` line. Additionally neutralize inherited `AIVERSE_GEOMETRY_POOL`, `AIVERSE_RENDER_SAFE`, `AIVERSE_AUTO_RIGID_MERGE`, `AIVERSE_AUTO_SHADOW_BATCHES` and `AIVERSE_AUTO_MATERIAL_BATCHES` controls; set `AIVERSE_AUTOLOD=off` only in the benchmark subprocess to avoid generated coarse geometry introducing a new confound.
2. `tools/batching-stage129-analyze.py`: process `mesh1/` and `mesh4/`, not `mesh8/`; update synthetic `--self-test`. The unchanged Stage127 analyzer still requires exactly 66 valid completed GPU command-buffer observations and 81 measured CPU frames for **every** run. Actual Direct vs MDI topology remains strict: Direct 0 ICB commands; forced MDI 18 ICB submissions, 4086 commands and 14/14 shadow/opaque draw groups for each measured frame.
3. `tests/render/material_batches.cpp`: after actual `render::uploadModel`, emit a diagnostic of `model->geometryPoolPage`, `gpuPoolVertexBytes` and `gpuPoolIndexBytes`. The latter GPU allocation fields are used instead of `vertices.size()` and `indices.size()` because ordinary `uploadModel` clears CPU vectors by default. No renderer, Metal ICB, Vulkan/GLES, RHI, shader or pipeline code changed.

The fixture still accepts N=8 as an isolated research option. **Do not use N=8 for the controlled pooled A/B test**. Even N=4 may fail the pool gate if model preparation or backend eligibility changes: treat that as a **failed experiment** and inspect logs, not as evidence that GPU MDI is slower/faster.

## Tests and evidence — exact scope

**Actually performed in Stage131:** inspected checked-in `ModelVertex` layout, `GlobalGeometryPool.cpp` admission policy, `Model.cpp::uploadModel` CPU-copy lifetime, Stage130 interleaved runner and Stage129 analyzer through GitHub; created the isolated branch and code commits; confirmed via GitHub compare that only the three named test/runner files changed before this handoff. Independently executed a Python reproduction of the tessellation point-and-triangle loop for N=1,2,4,8: 1/4/16/64 counterclockwise triangles, identical unit-triangle oriented area, authored byte sizes above. Performed source-anchor checks on all modified files; confirmed a single runner archive emission and both N4 analyzer references.

**Not performed by assistant:** C++ compilation, checked-in `bash -n` or Python bytecode/self-test, real `aiverse_material_batches --unit`, 16 native Mac Studio Metal processes, GPU timing, pixel parity, runtime Geometry Pool admission, full CTest, Vulkan/GLES builds. Stage129/130 CI results are not a substitute for owner-device performance, and the script must abort if pool admission or raster invariants fail. **PR #77 stays Draft.**

## Mac Studio acceptance — new detached worktree

Use a separate worktree; do not disturb the user's TerraWeave, Stage127/129 worktrees, open rebases, or captured archives.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage131"
git -C "$ROOT" fetch origin perf/batching-stage131-pooled-geometry-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/batching-stage131-pooled-geometry-20261120 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage129" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage129" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage129-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py tools/batching-stage129-analyze.py || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
build/batching-stage129/aiverse_material_batches --unit || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage129/aiverse_material_batches \
  bash tools/batching-stage129-trials.sh
```

Success demands 16/16 independent process PASS with `pooled=1`, exact topology and per-run parity, 1056 valid completed whole-GPU-CB samples and 1296 measured CPU frames; results are **acceptance targets**, not yet recorded observations. Archive should contain logs, GPU/CPU CSV, PPM captures, `stage127-comparison.csv` in mesh1/mesh4 and `stage129-summary.csv` plus `stage129-scaling.csv` at capture root. Share `Stage129 archive: ...tar.gz` or first compiler/runtime failure. Inherited Metal ICB pipeline stays process opt-in; no production AUTO/MDI enablement.

## Next agent

- If CI or Mac compile fails, capture exact first error and fix without weakening Metal buffer-binding/resource-residency checks.
- Verify both N1 and N4 `pooled=1` *and* identical MDI command topology. If pool availability differs, stop; do not interpret scaling as geometry-only.
- Compare two independent process medians and P95 for each cell, matched Direct/MDI premium and both inherited-pipeline modes; separate GPU whole-command-buffer from CPU wall and shader cost. The experiment changes tessellation/vertex fetch/raster work **together**, not isolated vertex ALU.
- Only after native validation, merge PR #77 into PR #75, then evaluate the encompassing PR #39 separately. Keep all Draft until checks and representative content demonstrate stability/performance. For each further stage, commit a new handoff with exact branch/SHA, files, actual tests and next step.
