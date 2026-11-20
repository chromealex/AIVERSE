# AIVERSE — Stage 141: empty indexed-ICB execution-tail experiment

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage141-empty-icb-tail-20261120`  
**Draft PR:** [#93](https://github.com/chromealex/AIVERSE/pull/93), targeting `perf/batching-stage140-native-depth-uniform-results-20261120` (Stage140 Draft PR #92). **Do not merge directly into main** or merge the rejected Stage139 optimization into production.  
**Exact starting source HEAD:** `78cb4151b8a0403aed336e1c93822827f27f6f93`.  
**Latest source change before this handoff:** `169e291e1e14ff70595a51371205626f35f9d123`; the handoff itself is a subsequent commit.

## Engineering question and prior measured evidence

Stage124 already changed the number of *real* indexed ICB draws but also changed authored geometry/primitive counts, so it could not isolate empty-slot playback. Stage122 varied ICB group size but also changed command count/residual Direct draws. Stage136, 138 and 140 owner-native Mac Studio archives show that CPU Metal MDI can save ~44–51% render CPU wall time while its full completed GPU command buffer is about 19–25% slower than Direct at N4. Stage140 tested omitting fragment bindings for genuinely fragmentless depth pipelines (`AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS=1`) and found **no GPU benefit**: matched MDI GPU premium increased approximately 0.082ms; the Stage139 experimental path must remain OFF.

The new question is narrower: when *all real geometry, shaders, ICB command contents, buffer state and allocation/reset ranges stay the same*, does asking Metal to execute a longer ICB range that contains only 128 additional **reset/unencoded (empty)** entries per submission add significant whole-CB GPU time? Apple explicitly documents that ICBs may contain empty commands; see [Apple MTLBlitCommandEncoder optimizeIndirectCommandBuffer:withRange:](https://developer.apple.com/documentation/metal/mtlblitcommandencoder/optimizeindirectcommandbuffer%3Awithrange%3A). **Stage141 does not call that optimization method or insert blit encoders.** It only tests existing ICB execution-range behavior.

The experiment is falsifiable but cannot prove per-active-command GPU overhead: empty commands may already be skipped cheaply by the driver. Whole completed Metal CB timing still includes all passes and GPU scheduling, not isolated model/ICB execution.

## Modified production backend, experimental default OFF

**File:** `engine/gfx/metal/GfxMetal.mm`.

New process-latched environment control:

```bash
AIVERSE_METAL_CPU_MDI_EMPTY_TAIL=0
AIVERSE_METAL_CPU_MDI_EMPTY_TAIL=128
```

**Unset**: exact original capacity, reset, and executed range of `drawCount` only; no additional allocation or reset. Invalid strings also leave experiment inactive. With either accepted explicit setting (0 or 128), each CPU-encoded Metal indexed ICB submission:

1. Retains the same original descriptor (`inheritBuffers=NO`, `inheritPipelineState` resolved by the old flag, `maxVertexBufferBindCount=9`, `maxFragmentBufferBindCount=9`). The Stage141 native runner explicitly forces inherited pipeline OFF.
2. Allocates or reuses an ICB with capacity at least `drawCount + 128` and invokes `[icb resetWithRange:NSMakeRange(0, drawCount+128)]` in **both** experiment modes. A bounded `uint32_t` overflow check guards arithmetic.
3. Encodes the exact same `drawCount` original `drawIndexedPrimitives` calls at slots 0..`drawCount-1`; **no commands are encoded in the last 128 reset slots**.
4. Uses `[enc executeCommandsInBuffer:icb withRange:NSMakeRange(0, drawCount)]` when explicitly set to `0`, or `drawCount+128` when set to `128`. This is the **one intended experimental variable**. Descriptor, capacity, reset range, CPU writes, uniforms, indexed draw list, shaders, material state and framebuffer workload are identical between control/experiment.
5. Logs once per real CPU MDI process, after a genuine submission: `[metal-icb-empty-tail] mode=off|on valid=N reset=N+128 execute=N|N+128`. The analyzer requires valid positive N and verifies the exact arithmetic. It fails if the instrumented path was not actually entered.

**Do not change** per-command vertex buffers, fragment/PBR bindings, `declareIndirectRead` resource residency, GPU in-flight uniform lifetime, ICB frame-slot/fence reuse, or Direct fallback; the previous AGX G15 crash with inherited buffers is a hard safety constraint.

The branch starts from Stage140, which includes Stage139's rejected fragmentless flag in source, but **the Stage141 runner explicitly unsets that flag for every subprocess**. Default behavior of both opt-ins remains OFF. Before any eventual production merge, rebase an independently validated successful optimization onto a clean base without the rejected Stage139 experiment.

## Files and APIs

- `engine/gfx/metal/GfxMetal.mm`: single-variable opt-in ICB execution-range padding. A small additional `<limits>` include supports explicit uint32 overflow bounds.
- `tools/batching-stage141-trials.sh`: 12 genuinely separate macOS processes, order `off-direct1,off-mdi1,on-direct1,on-mdi1,on-mdi2,on-direct2,off-mdi2,off-direct2,off-direct3,off-mdi3,on-direct3,on-mdi3`; 3 repeats for each OFF/ON × Direct/MDI cell. Disables old Stage139 flag, pipeline inheritance, AUTO and GPU-authored ICB. Fixed 2048 authored unique model primitives, N=4 tessellation, triangleScale=4, shared Geometry Pool 5,898,240 vertex bytes + 393,216 index bytes; full shadow+opaque passes; diagnostic group cap=0; 18 MDI submissions/4086 real commands per measured MDI frame. Runs `--unit` before processes, captures failure archives, optional exact source/binary/macOS provenance and strict parity/metadata gates.
- `tools/batching-stage141-analyze.py`: Python3 standard-library only. Uses `runpy` to load the existing Stage127 per-frame GPU/CPU and Direct/MDI draw topology verifier. Independently checks 12 process logs, actual Metal audit marker arithmetic, PPM before/after and actual MDI frame parity, 66 valid whole-GPU-CB timestamps per process, 81 measured CPU frames per process, shared geometry pool and provenance. Outputs `stage141-runs.csv`, `stage141-paired.csv` and `stage141-summary.csv` with GPU median/P95, CPU medians, three matched-control MDI premium changes (positive = empty-tail ON faster). Its `--self-test` constructs 12 synthetic log/GPU/CPU/PPM cases, validates a 0.03ms synthetic premium benefit and deliberately rejects an incorrect ICB reset range and an altered actual MDI image.
- `tools/batching-stage141-native-mac.sh`: one-command Mac Studio launcher for new clean detached worktree, Release CMake rebuild, machine/source/compiled binary provenance and SHA-256, `bash -n`, Python bytecode/self-test, model-batching `--unit`, then the full 12-process experiment.
- `.github/workflows/gpu-batching.yml`: extends the existing `metal-cpu-mdi` job with both Stage141 shell syntax checks, Stage141 Python bytecode/self-test, plus existing native fixture build and raster regressions. It does **not** run 12 repeat performance cases on shared hosted runners.
- `docs/agent-handoffs/batching-stage141-empty-icb-playback-2026-11-20.md`: this handoff.

Expected sample totals **if native acceptance succeeds**, not yet observed: 12×66 = **792** valid completed whole-Metal-CB GPU timestamps; 12×81 = **972** measured CPU frames; six actual MDI processes with 18 submissions/4086 *real* indirect commands per measured frame. Both OFF/ON MDI use 18×128=**2,304 padded reset slots** per frame. ON executes a total ICB range of **6,390** slots per frame (4,086 real + 2,304 empty); OFF executes 4,086 real slots. The underlying active geometry and Direct/MDI raster should remain unchanged.

## Actual tests and validation boundary

**Actually completed in this Stage141 coding step:**
- Reopened Stage120/122/124 and Stage140 GitHub handoffs and audited the actual current Metal `drawIndexedIndirect` path. Confirmed prior group-cap and real-command scaling experiments do not isolate an empty-slot execution range.
- Verified Apple's official API explicitly describes empty ICB commands and notes optimization may remove them. No driver optimization or performance improvement is assumed.
- Created isolated branch on exact tested-data Stage140 parent, changed one Metal function, added standalone tools and CI gates. Re-fetched the edited source and manually verified `inheritBuffers=NO`, unchanged active indexed-command loop, unchanged vertex/fragment uniform bindings and `declareIndirectRead`, and identical **padded allocation/reset ranges** for both experiment modes.
- Independently executed a Python arithmetic/matrix check: twelve distinct independent process identities (3 per OFF/ON Direct/MDI cell), logical 4,086 real indirect commands and eighteen submissions, 2,304 additional reset slots per MDI measured frame, OFF execute range total 4,086, ON 6,390. Check result `PASS`.
- Re-fetched Stage141 runner, analyzer and native launcher from GitHub and compared stage IDs, flags, provenance fields and marker regex. Rechecked only five intended source file changes before this handoff.

**Not yet done:** native Mac AppleClang/Objective-C++ compilation of modified Metal source; execution of the checked-in Stage141 Python `--self-test` and `bash -n` on the downloaded checkout; native 12-process Metal GPU capture; GPU timings; pixel parity on target Apple GPU; full CTest; Vulkan/GLES builds or representative real-scene FPS. GitHub Actions may still be queued; check exact final HEAD before claiming PASS. **No GPU optimization benefit or stable causal estimate is claimed.**

## Mac Studio command — fresh isolated worktree

Do not reset, switch, rebase or overwrite ongoing `main`, TerraWeave, editor, Stage127–140 or other project worktrees.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage141"
git -C "$ROOT" fetch origin perf/batching-stage141-empty-icb-tail-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" \
  origin/perf/batching-stage141-empty-icb-tail-20261120 || exit 1
cd "$WT" || exit 1
bash tools/batching-stage141-native-mac.sh
```

Share **`Stage141 archive: ...tar.gz` and the actual archive**, or send the **first real compiler, Python or GPU error** (partial archive retained). If native Metal faults with a padded ICB range or raster parity fails, keep default OFF and stop; never bypass buffers/residency/fence protections.

## Next agent

1. Check GitHub Actions on exact last Stage141 commit, inspect first failing job/step and fix only grounded failures. Retain this experiment's default OFF and Draft PR.
2. On owner Mac Studio, validate all real GPU logs/PPM and raw CSV, then compute the three independent matched Direct controls, GPU medians/P95, CPU medians and OFF-vs-ON MDI *premium* differences. If any log is a no-op/incorrect range, reject rather than summarizing timings.
3. If ON is consistently slower, empty-slot traversal contributes a GPU penalty under these controlled circumstances. If ON is similar, empty slots may be efficiently skipped; active indexed draw playback or resource/vertex state remain potential drivers. Neither outcome directly isolates model-pass GPU cost, and three repeats are not proof of statistical significance.
4. Do not enable Stage139, Stage141, CPU MDI or AUTO in production on this synthetic evidence. Preserve `inheritBuffers=NO`, all explicit buffers and residency/fences. Before adopting a genuinely safe performance improvement, validate representative real scenes plus Metal/Vulkan/GLES functional behavior.
5. After any next development step, commit a **new independent handoff** with exact branch/full SHA, architecture/API, modified paths, actual tests/results, limitations and next action.
