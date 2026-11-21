# AIVERSE — Stage 129: fixed-command geometry workload vs CPU Metal MDI

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Implementation branch:** `perf/batching-stage129-vertex-workload-20261120`  
**Draft PR:** [#75](https://github.com/chromealex/AIVERSE/pull/75), targeted at `perf/auto-batch-cost-metrics-20261119` (Draft [#39](https://github.com/chromealex/AIVERSE/pull/39)). **Do not merge into main directly.**  
**Base SHA:** `24ae6e1f998c1c9541ca7600ee40b5874756b2ec`  
**Pre-handoff source HEAD:** `5e094c96a1e10b6f52f8a6ee7a6a82777a822413` (this handoff commit follows).

## Background and measured evidence

Stage128's user Mac Studio native eight-run inherited-pipeline trial yielded, in whole completed Metal GPU command-buffer medians: OFF Direct 0.5702195ms / MDI 0.819625ms (MDI +43.74%); ON Direct 0.569854ms / MDI 0.7822395ms (MDI +37.27%). Inherited pipeline improves MDI ~4.56% but **does not close** the GPU premium. Stage128 analyzer label bug was fixed, but the user's raw native archive analysis still needed the corrected Python rerun.

Stages119–128 do not establish the root cause of the GPU ICB regression. The Stage125 Low/High shader-quality variant did not materially distinguish the premium; hardware per-pass GPU counters and Xcode GPU replayer were unavailable/crashing.

## Goal and controlled test

Hold fixed: 2048 model meshes/nodes, one material, node positions and transforms, full shadow+opaque pass topology, CPU MDI grouping, **18 actual submissions / 4086 actual ICB commands per measured frame** (when forced MDI), Direct control, shaders/pipeline semantics within each pipeline mode, and fixed-quality PBR material.

Change **only test-fixture geometry density**: a regular subdivision of each original triangular primitive into `N*N` independent triangles; `N=1` gives 2048 original triangles, `N=8` gives 131072 triangles. Each mesh remains ONE `Primitive` / draw command, with more vertices/indices. Unit checks also cover N=2 and N=4. The total rendered silhouette is intended to remain the same, but floating-point rasterization, vertex fetch, vertex processing, small-triangle behavior and possibly fragment work can all vary. This is a **geometry workload sensitivity experiment, not a pure vertex ALU counter**; it is not representative-game-scene evidence.

Matrix: N=1 and N=8; inherited Metal ICB pipeline OFF/ON; Direct/MDI; 2 independent processes per cell = **16 processes**. The runner uses counterbalanced order within each N as Stage127, demands 38 CPU invariants per process, 66 valid completed GPU timestamps, 81 measured CPU frames, actual frame-100 MDI pixel parity and final screenshot parity. Each N invokes unchanged Stage127 process analyzer; Stage129 separately compares Direct-vs-MDI premiums and geometry-scaling deltas, including a synthetic self-test.

## Changes and API

- `tests/render/material_batches.cpp`: test-only `AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS=1|2|4|8`, default `1`, applies only to `AIVERSE_PALETTE_TEST_CASE=auto_stress`; reject more than one million generated triangles. The default 16-primitive fixture remains structurally unchanged (N=1). Add unit-mode topology checks and a diagnostic line `stress diagnostic mesh subdivisions=... triangles_per_primitive=... vertices_per_primitive=...`. No change to renderer/MultiDraw/Metal backend or shader code.
- `tools/batching-stage129-trials.sh`: new isolated capture folder, provenance manifest, `--unit` stale-binary guard, 16 strict native cases, Stage127 analyzer per N, Stage129 combined analyzer, failure archives.
- `tools/batching-stage129-analyze.py`: parse validated Stage127 summaries for mesh1/mesh8, verify 18/4086 topology, compute paired GPU and CPU medians and GPU-premium scaling, emit `stage129-summary.csv` and `stage129-scaling.csv`; `--self-test` generates artificial CSV controls only.
- `docs/agent-handoffs/batching-stage129-vertex-workload-2026-11-20.md`: this handoff.

Commits before handoff: `57f68c9` fixture, `cc376bf` trial script, `9ea1f04` analysis, `e8b3afa` unit checks, `5d5fa74` newline fix, `59190e8` stale-binary gate, `5e094c9` provenance wording. Branch is isolated from owners' currently validated TerraWeave worktree and does not change Stage127 source.

## Validation truth

**Actually performed:** GitHub source and dependency inspection, explicit branch compare confirming only test fixture + two tool scripts differ from PR #39, independent Python calculation of regular tessellation counts/winding/area (N=1/2/4/8 generate 1/4/16/64 consistently oriented triangles per primitive; constant area). The later native tests below are **not executed** by the assistant.

**Not performed:** native C++/Objective-C++ compile, running the new `--unit` checks, `bash -n`, `python3 -m py_compile`, analyzer self-test, 16 real Metal GPU runs, captured raster parity on N=8, full CTest, Vulkan/GLES runtime or compilation, realistic workload benchmark. None of the Stage128 owner timings prove Stage129 performance. PR #75 must remain Draft.

## Native Mac Studio validation — new detached worktree

Do **not** reset the primary checkout, interrupt a merge/rebase, or modify the Stage127 capture. Use a fresh worktree and **rebuild** the benchmark: the new fixture does not exist in the Stage127 binary.

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage129
git -C "$ROOT" fetch origin perf/batching-stage129-vertex-workload-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/batching-stage129-vertex-workload-20261120 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage129" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage129" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage129-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py tools/batching-stage129-analyze.py || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
build/batching-stage129/aiverse_material_batches --unit || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage129/aiverse_material_batches \
  bash tools/batching-stage129-trials.sh
```

On success share the emitted `Stage129 archive: ...tar.gz` or the summary CSV. Confirm `result=PASS` in the archived manifest and exact N=1/N=8 parity/topology before interpreting timing. If native compile fails send first error; if N=8 MDI parity or hardware command encoding fails, **stop and preserve archive** instead of relaxing test guards. Re-running original Stage127 archive analysis with corrected analyzer is still desirable separately.

## What next agent should do

1. Verify Mac native build, analyzer self-test, CPU-only fixture tests and complete native matrix; fix test harness only with evidence, preserving renderer defaults.
2. Interpret both matched-control **GPU CB premium** and scaling: whether Direct and MDI increase similarly with tessellation, or MDI incurs disproportionate additional time. Two repetitions per cell is an initial indicator, not proof of causality.
3. Add realistic textured/mixed-material/LOD scenes and repeat on supported backends/hardware. Keep AGX crash mitigations, explicit vertex/fragment buffers, residency declarations, lifetime fencing, and fallback behavior. Do not enable MDI or inherited pipeline by default until proven faster and stable on representative content.
4. Append a new separate handoff after further work, including changed files, commands actually run, results, constraints, exact branch and commit.
