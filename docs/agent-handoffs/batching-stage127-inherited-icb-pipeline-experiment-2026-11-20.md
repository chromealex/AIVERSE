# AIVERSE — Stage 127: opt-in CPU Metal ICB inherited pipeline A/B

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft.

## Findings prior to modification

Owner native Stage125 Low/High archive (`capture-20261120T094744Z-79217.tar.gz`) passed 8/8 runs with 528 valid whole-CB GPU timestamps and pixel parity. Both quality variants exhibited 44–45% **GPU CB regression** for CPU MDI versus Direct, while reducing CPU render wall by 40–42%. Across Stages119–125 this GPU regression was robust to model pass selection, group cap, number of commands at fixed submissions and material quality. Root cause remains **unproven**, and GPU pass-only timestamps/driver profiler are unsupported/unreliable on owner's Mac Studio Xcode 26.3.

GitHub review of `engine/gfx/metal/GfxMetal.mm` found the CPU-encoded ICB path binds one pipeline state with `[command setRenderPipelineState:indirectPipeline]` for *every individual command*. All commands of a CPU MDI submission already share the same validated pipeline, and each one explicitly binds vertex/fragment uniform buffers and vertex geometry. Metal ICB descriptor currently `inheritBuffers=NO`, `inheritPipelineState=NO`; a historical AGX G15 crash was triggered by **buffer inheritance/zero vertex slot counts**, so the explicit buffer bindings, resource residency and frame fences are safety requirements and are left unchanged.

## Implemented experiment

**Engine source:** commit `8e4751ae44c40f9b61d263df432c18ab4d434215` changed only `engine/gfx/metal/GfxMetal.mm`:

- New process opt-in `AIVERSE_METAL_CPU_MDI_INHERIT_PIPELINE=1`, read once before the descriptor is initialized; default OFF.
- When OFF: `inheritPipelineState=NO`, every command continues `setRenderPipelineState:indirectPipeline` exactly as before.
- When ON: `inheritPipelineState=YES` and **per-command pipeline recording is omitted**, with parent `[enc_ setRenderPipelineState:indirectPipeline]` directly before `executeCommandsInBuffer:withRange:`; `p->state` is restored immediately after the execution call to preserve subsequent Direct fallback.
- No changes to `inheritBuffers=NO`, `maxVertexBufferBindCount=9`, `maxFragmentBufferBindCount=9`, explicit per-command vertex/fragment buffer slots, indices, PBR descriptor resources, `declareIndirectRead`, ICB reset, uniform snapshots, command validation or frame synchronization. The PBR variant inherits the same `icbPbrState` that each command previously bound explicitly, and restores the ordinary pipeline.
- The Metal CPU ICB descriptor is immutable within a process; the flag should not be flipped mid-run, nor should ICBs from two modes share one descriptor. Each A/B trial uses a separate process.

**Test runner:** `tools/batching-stage127-trials.sh`, commits `40f6cf2d2074c5251fa879c630186f5c2a780d37`, `a9f616716e493461891187dfbaccb4fea479b5fd`.
- Eight independent processes, order `off-direct1,off-mdi1,on-direct1,on-mdi1,on-mdi2,on-direct2,off-mdi2,off-direct2`, 2048 unique primitives, full shadow+opaque model passes, original grouping (0 diagnostic cap), material quality kept at default for all variants.
- Ensures forced Direct and forced MDI are genuine, 81 measured frames each; MDI has 18 actual submissions per measured frame (1458 accumulated), valid 66 whole completed GPU CB timestamps, 38 CPU invariant checks, terminal screenshot `changed_channels=0` and additional forced-MDI frame-100 raster parity. Preserves failure diagnostics and partial archive with logs.
- Note: Direct mode ignores ICB pipeline flag; it is a control for run-to-run scheduling/drift. The ON setting is an experimental **GPU driver behavior**. A genuine Metal GPU failure, pixel mismatch or submission-count change is a rejection, not permission to remove other resource-safety safeguards.

**Analyzer:** `tools/batching-stage127-analyze.py`, commit `9e4d94356d36a34b0bd4fc4d5b15faee4b05c177`; validates exactly 66 valid GPU CB timestamps and unchanged 81-frame measured Direct/MDI model topology (Direct 2048+2048 model draws and zero indirect; MDI 18 ICB submissions, 4086 indirect commands, 14+14 model calls), calculates mode/pipeline-mode medians, P95 and CPU render wall, writes `stage127-runs.csv` and `stage127-comparison.csv`.

## Validation truth

**Performed:** GitHub source inspection of Metal CPU ICB setup/command encode/parent execute, coding a default-off pipeline-only inheritance experiment and comparator scripts, static string/anchor checks for patch positions and model-runner expectations. **NOT performed:** native AppleClang Objective-C++ compilation, 8-case Mac GPU test, pixel parity with inherited pipeline ON, script `bash -n` and `python3 -m py_compile` in the native repository, full CTest, Vulkan/GLES. Do not claim the experiment is safe/performance-positive on device until its native logs confirm both.

## Mac Studio validation, in a new isolated detached worktree

This source change **requires a new native build**, unlike Stage125; do **not** reuse the Stage121 binary.

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage127
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage127" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage127" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage127-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage127/aiverse_material_batches \
  bash tools/batching-stage127-trials.sh
```

If the run succeeds, share `Stage127 archive: ...tar.gz`, containing `stage127-comparison.csv` and all valid GPU/CPU per-frame timelines and parity captures. If compilation fails, share the first compiler error; if native Metal fails, capture native logs/crash report and stop the ON trial. **No known GPU performance improvement has yet been demonstrated.**

## Next agent acceptance gate

Compare OFF/ON CPU MDI GPU CB median/P95 *and* their corresponding Direct controls. Confirm consistent model topology and pixel parity in **each** run. A GPU time reduction must repeat across both ON processes and must not introduce crashes or raster changes; if not, leave option disabled or revert experimental code. Even a synthetic-scene GPU win does not justify enabling MDI by default until representative engine content is tested across applicable Metal hardware. Any later code stage needs its own handoff with branch/commit, changes, actual tests and next step. PR #39 remains Draft.
