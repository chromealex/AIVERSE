# AIVERSE — Stage 139: opt-in fragmentless-depth Metal ICB uniform binding A/B

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Implementation branch:** `perf/batching-stage139-depth-fragment-icb-20261120`  
**Draft PR:** [#90](https://github.com/chromealex/AIVERSE/pull/90), targeting `perf/batching-stage138-native-raster-results-20261120` (Draft PR #88). Do not merge directly into `main`.  
**Exact starting SHA:** `0a1cc1884f7420513043be39d2f646b29d14017d`.  
**Implementation HEAD before this handoff:** `026734f7b9a966764a383820ed38751793236c97` (this handoff commit follows).  
**Production default:** CPU MDI, AUTO, inherited ICB pipeline, and the new fragmentless experiment remain **OFF unless explicitly opted in**.

## Native evidence and motivation

Stage138 analyzed a real Mac Studio archive `capture-20261120T123130Z-56024.tar.gz` (original SHA-256 `ef541af5c0bfaf51072b7aa7bbecde833c4446c269950fb1ede9c9ef2a58242c`). It verified 16/16 Metal A/B processes, 1,056 valid completed whole-command-buffer GPU timestamps and 1,296 measured CPU render frames at N4 with 2048 unique primitives, 18 ICB submissions / 4086 actual MDI commands per measured frame. Across raster scales and inherited-pipeline modes, CPU MDI cut CPU render wall by ~44–51%, but the completed Metal GPU command buffer was ~19.6–21.2% slower than Direct. Coverage-dependent GPU premium changes were not consistent across independent repeats, and the mechanism remains **unproven**. Stage136's tessellation scaling also did not isolate driver ICB playback.

A source audit of `engine/gfx/metal/GfxMetal.mm` showed that the classic CPU ICB binds the same uniform buffers to both **vertex and fragment stages in every command**. Metal's pipeline factory explicitly permits an ICB with no fragment shader (`fs == nil`) in depth-only use. In that narrow case, the fragment binding has no fragment consumer, but the vertex binding is still essential. This suggests a narrowly reversible *per-command encoding/state cost* experiment.

## Metal backend change

**File:** `engine/gfx/metal/GfxMetal.mm`.

New process-level flag:

```bash
AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS=1
```

- Default **unset**: production uses exactly the original fragment uniform binding logic, with no extra shader lookup.
- Literal `0` and `1` both enable a lightweight experiment audit. Literal `0` is the control, leaving the full original ICB command bindings; literal `1` tests omission of fragment uniform bindings.
- The omission is eligible **only** if all conditions hold: `p->desc.depthOnly`, `p->icbPbrState == nil`, a live shader registry entry for the actual `p->desc.shader`, and `shader->fs == nil`. Missing metadata or any fragment function falls back to the original bindings.
- For eligible depth-only commands only, `[command setFragmentBuffer:binding.buffer offset:0 atIndex:binding.index]` is skipped. Every `setVertexBuffer` binding remains in the command. PBR argument buffers, their fragment binding, all user uniforms for fragment-capable passes, render pipeline state and drawIndexed calls remain unchanged.
- Metal ICB descriptor retains `inheritBuffers=NO`, `maxVertexBufferBindCount=9`, `maxFragmentBufferBindCount=9`; all `declareIndirectRead` residency declarations, uniform snapshot ownership, ICB reset, command bound validation and existing frame fences are unchanged. **Never restore buffer inheritance**: previous Apple AGX G15 faults occurred with inherited buffers / zero binding count.
- First eligible MDI call prints a *single* marker per process: `[metal-icb-depth-uniforms] mode=off|on eligible=1 slots=N`. The native runner requires `N > 0` in MDI modes. If the actual pipeline does not have a fragmentless depth stage (or the optimization is not exercised), the test **fails** instead of claiming performance evidence.

No Vulkan/GLES/WebGL, renderer or game-genre code is modified.

## New test tools / architecture

**`tools/batching-stage139-trials.sh`**: twelve distinct processes in mirrored/counterbalanced order:
`off-direct1,off-mdi1,on-direct1,on-mdi1,on-mdi2,on-direct2,off-mdi2,off-direct2,off-direct3,off-mdi3,on-direct3,on-mdi3`.

Factor: fragment uniform skip OFF/ON; mode: forced actual Direct/MDI; **three independent processes per cell**. Every run uses 2048 authored unique model primitives at N4 with triangle scale4; same 32,768 authored triangles, geometry-pool bytes **5,898,240 vertex + 393,216 index**, same material/shader and camera, full shadow+opaque passes and MDI grouping cap=0. Pipeline state inheritance is always forced **OFF** to avoid Stage127 confounding. AUTO/GPU-authored ICB are not enabled.

Strict per-process gates: material palette 38 CPU invariants passed, `--unit` preflight, expected pooled geometry, exact depth diagnostic eligibility with positive uniform slot count for real MDI, mode-specific 81 measured Direct/MDI frames, real 18 ICB submissions and 4,086 commands per MDI measured frame, 66 completed tagged GPU CB timestamps, 106 total CPU timeline records, pixel parity and a separate actual MDI capture frame. The runner archives logs, original CPU/GPU CSV and before/after/MDI PPM files, preserving partial results on errors. When launched natively it embeds host/toolchain/source/executable provenance as `native-provenance.txt`.

**`tools/batching-stage139-analyze.py`**: Python3 standard library only. Dynamically loads the *existing* Stage127 strict per-frame parser `tools/batching-stage127-analyze.py` to reject missing/invalid GPU rows, unstable or unexpected topology and wrong sample counts. Independently verifies manifest/provenance, experiment eligibility markers, exact PPM bytes/hash parity, and real Direct/MDI logs. Writes:
- `stage139-runs.csv`: 12 individual process GPU median/P95/max, CPU median/P95, topology and eligible uniform slot count.
- `stage139-paired.csv`: three repeat-paired comparisons of Direct/MDI in skip OFF/ON, relative GPU premium change and CPU/P95 controls.
- `stage139-summary.csv`: medians of three independent process medians and CPU differences.

Its `--self-test` generates all 12 synthetic GPU/CPU timelines and 1×1 parity images, verifies a synthetic 0.03ms matched premium benefit and rejects one-byte corruption of an actual MDI image. Synthetic observations are not device performance data.

**`tools/batching-stage139-native-mac.sh`**: one-command macOS-only worktree guard, clean-tree check, Release CMake configure/build, exact executable SHA-256 and machine/compiler provenance, shell syntax/Python compiler and synthetic self-tests, native `--unit` and the full 12-case benchmark.

**`.github/workflows/gpu-batching.yml`**: adds Stage139 trial and launcher `bash -n`, Stage139 analyzer `python3 -m py_compile`, Stage139 analyzer `--self-test` to the existing macOS focused GPU batching CI fixture. CI raster checks remain in place. The GitHub runner does not substitute for Mac Studio performance measurements.

## Actual validation versus unverified items

**Performed in Stage139:** fetched current GitHub Metal backend and Stage138 handoff; inspected Metal shader pipeline creation and verified `pd.fragmentFunction=s->fs` with `nil` for depth-only fragmentless shaders; confirmed that the new skip is guarded by `depthOnly && !pbr && shader && shader->fs == nil` and `AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS=1`; confirmed buffer inheritance remains `NO` and explicit fragment/vertex residency untouched. Verified branch diff contains only Metal source, new tools and CI. Audited the runner's intended 12 entries, exact 18/4086 topology, a single archive endpoint and a single terminal `result=PASS`. A faulty initial tool replacement temporarily duplicated the script tail; it was corrected in commit `026734f7b9a966764a383820ed38751793236c97` by reconstructing the original runner and inserting only the provenance block. The final checked-in runner was re-read after correction and contains **one** trial loop and one archive endpoint.

**Not performed:** assistant-side checked-in Python self-test execution, checked-in `bash -n`, native AppleClang/Objective-C++ compile of the changed `GfxMetal.mm`, 12 native Mac GPU processes, real fragmentless-path marker, real raster PPM parity, Stage139 GPU/CPU timings, full CTest or Vulkan/GLES build/runtime tests. **No GPU improvement is claimed.** The macOS CI job is expected to compile/test the scripts and GPU regression but requires an actual completed result before claims of passing.

## Owner Mac Studio validation

Use a **new detached worktree**; do not reset or alter any of the user's TerraWeave or concurrent AIVERSE worktrees, unfinished merges or earlier capture archives.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage139"
git -C "$ROOT" fetch origin perf/batching-stage139-depth-fragment-icb-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" \
  origin/perf/batching-stage139-depth-fragment-icb-20261120 || exit 1
cd "$WT" || exit 1
bash tools/batching-stage139-native-mac.sh
```

If successful, share **`Stage139 archive: ...tar.gz` and the actual archive**. Expected gates (targets, NOT observed): 12/12 processes, **792 completed whole-Metal-GPU-CB samples**, **972 measured CPU render frames**, six actual MDI processes at 18 submissions / 4086 commands per measured frame, six Direct controls, fixed pooled GPU geometry, proper depth-no-fragment markers with positive slot count, pixel-perfect parity within each run and three paired process comparisons.

If a marker is missing, stop and send partial archive; the real shadow pass may use a fragment function on this backend and the experiment would be a no-op. Do not relax that assertion to force an uninformative result. If any Metal fault occurs, preserve crash/partial logs, do not remove AGX safeguards.

## Next agent

1. Inspect exact current HEAD of PR #90 and focused GitHub CI; if failed, use first compiler/script error to fix in a new stage with handoff.
2. Read the owner's Stage139 archive and independently recompute all 12 GPU medians/P95 from real frame CSV, CPU medians, exact MDI command topology and per-process PPM parity.
3. Compare ON vs OFF **MDI premium against its matched Direct control** over all three independent repeats. A GPU benefit must be stable, not just one favorable aggregate; CPU improvement alone does not establish a GPU win. Report uncertainty, hardware/compiler provenance, and avoid conflating whole GPU CB time with specific model-pass or shader stage time.
4. Even if a synthetic scene improves, validate representative textured/mixed-material/transparent/LOD models and Metal/Vulkan/GLES architecture before enabling production CPU MDI or AUTO. Keep per-command vertex binding, residency, fences and AGX crash mitigation.
5. At the next stage, create another `docs/agent-handoffs/batching-stageXXX-*.md` recording precise branch, SHA, files/API, **actually performed** tests, results, limitations and next action.
