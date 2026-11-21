# AIVERSE — Stage 119: controlled shadow / opaque model workload isolation

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft

## Objective and evidence

Stages 108/109 established, using valid completed Metal GPU command-buffer timestamps, that CPU MDI reduces CPU `renderer.render()` wall time but raises whole Metal GPU CB median durations by ~37% at 2048 primitives and ~54% at 4096. Stage111 attempted draw-boundary timestamp samples but the owner's Metal device reported them unsupported. Stage114 captured genuine .gputrace files. In Stage117, Xcode's GPU Replayer crashed when asked to Profile. Xcode 26.3 also has no `gpudebug` found via xcrun. Stage116/118 visual checks confirm two render encoders in both modes and 4098 Direct vs 30 CPU-MDI API draw calls; those statistics do not yield GPU timings.

The new path provides an **indirect diagnostic**, not a replacement for GPU counters: compare the existing reliable **whole GPU command-buffer duration** between two same-workload render modes (Direct/MDI) under **three distinct fixture workloads**:
- `full`: both shadow and main opaque *model* draws.
- `shadow`: shadow model draws retained, main opaque model draws omitted; floor/other scene drawing and render encoders remain.
- `opaque`: opaque model draws retained, shadow model draws omitted; shadow encoder clear and non-model draws remain.

**Critical caveat:** Changing one pass changes visibility, framebuffer contents, resource dependencies and GPU scheduling. A difference between full and one-pass timings is NOT a precise per-pass GPU timestamp. Interpret only the Direction/MDI difference **within each same fixture workload** and compare across variants as diagnostic hypothesis formation, not causal attribution of time to an individual pass.

## Changes and architecture

**Commits:**
- `4cd74381820e80fe77fb055e51daf5b9b107eece` — `engine/render/Renderer3D.h`: test-only `setDiagnosticModelPassMask(uint8_t)`, bits 1=shadow, 2=main opaque, default 3=normal unchanged. Explicit setter; never used in game runtime unless deliberately enabled.
- `b6802956ef148d919056e069420750a8af3e2b6a` — `engine/render/Renderer3DModels.cpp`: immediately skip eligible phase 0 or main-view phase 1 model draw path when corresponding diagnostic bit disabled. Other phases and auxiliary render views unaffected. Default 3 preserves all existing rendering and Metal commands.
- `a58efd38a7e99bd40d0c1003b6c24239235a5eb4` — `tests/render/material_batches.cpp`: opt-in stress-only env `AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full|shadow|opaque`. Invalid value fails explicitly. Set mask once at fixture init, **before both the baseline Direct capture and the subsequent forced-mode render**, ensuring within-execution before/after pixel parity tests remain meaningful.
- `d06c233d6c334c62c1630befeb6758b476e4fa42` — `tools/batching-stage119-analyze.py`: validates 66 valid asynchronous Metal CB GPU timestamps for frames 115..180 and 81 CPU render frames for 115..195, reports GPU CB median/P95 and CPU render median/P95, compares Direct vs MDI within each of the three workloads, writes `stage119-summary.csv`. No model-only GPU duration claimed.
- `e490f8689fc2b5590c01200aac2359e8ccb88cd9` — `tools/batching-stage119-trials.sh`: six processes at 2048 unique primitives, full/shadow/opaque crossed with Direct/MDI, always with full Metal timestamp API (not unsupported draw counters), pixel parity checks, real MDI submitted when required, unique output folder and archive on success; on error an ERR trap prints current native log and preserves partial archive.

No Metal RHI, ICB buffer binding, render pass load/store, GPU fence, uniform snapshot, resource residency or AUTO policy was changed. GPU capture is disabled. Explicit opt-in only.

## Validation performed and not performed

**Performed:** inspected current GitHub Renderer3D phase implementation and stress fixture; updated feature branch source and handoff with independent commits. Earlier owner native Metal 2K/4K raster and whole-CB tests PASSED on previous code. **Not performed:** native AppleClang compile of Stage119 source, full six-mode experiment, `bash -n` on exact runner or Python analyzer compilation on Mac, real game scene, Vulkan/GLES build/performance, actual per-pass GPU counter time. Do not claim runtime success before owner logs.

## Mac Studio steps (isolated worktree; preserve old rebases)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage119
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage119" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage119" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage119-trials.sh || exit 1
python3 -m py_compile tools/batching-stage119-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage119/aiverse_material_batches \
  bash tools/batching-stage119-trials.sh
```

If successful, the last line prints `Stage 119 archive: ...tar.gz`; upload that archive for analysis. If a run fails, capture its terminal diagnostics/archived logs. Do not rerun earlier GPU Frame Capture / Xcode Replayer, which is crashing on the owner's setup.

## Next steps for another agent

Evaluate six process GPU median/P95 distributions plus per-frame MDI count and pixel parity. A pass-specific change in relative GPU costs can identify a candidate to investigate but does **not** prove that a standalone pass has precisely that duration. Confirm any change with repeated counterbalanced process order and representative content. If shadow and opaque variants both exhibit GPU regression, investigate common Metal ICB command execution/vertex/fragment bindings before optimizing per-pass material shaders. If only one variant exhibits regression, inspect that pass's pipeline state and draw/cull strategy. Do not default-enable CPU MDI while whole-GPU-CB regression remains. PR stays Draft, separate handoffs after each stage.
