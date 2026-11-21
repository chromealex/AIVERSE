# AIVERSE — Stage 134: reproducible Mac Studio CPU MDI native acceptance

**Date:** 2026-11-20  
**Repo:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage134-native-acceptance-20261120`  
**PR:** [#82](https://github.com/chromealex/AIVERSE/pull/82) Draft, targeting Stage133 branch `perf/batching-stage133-archive-verification-20261120`, which is PR #79. Ancestry: Stage132 PR #78 → Stage131 PR #77 → Stage129 PR #75 → batching PR #39. **Do not merge directly into main.**  
**Pinned Stage133 base SHA:** `ddf080579655747c2342c7b5c7ea69a8036cee27`.  
**Implementation before handoff:** `5ae0c32b16792e22ed31be52bc433c01898411ef` (launcher), `b41db2adefb5ce703332b87d95bffbe55527081a` (initial runner, superseded), `6f94244fe33239cb1e2a3e62e7f2f0785c993eb6` (auditor), `544c820bf35cf376fb38e076ebd9659483953ed8` (CI), `4ca0abe70eefa17611a2718bb1158f98ca3b64f1` (correct runner without duplicated tail), `a1011d0d0a6bcaf04fd9c5496472366500323aae` (safe shell toolchain provenance).

## Motivation and current evidence

Stages127–128 on the owner's Mac Studio found CPU MDI reduced CPU render wall time but remained about 37–44% slower than Direct in **whole completed Metal GPU command buffer** time on an artificial 2048-primitive case. Inheriting the common ICB pipeline (Stage127) brought about 4.6% MDI GPU improvement but is still experimental and OFF by default; buffer inheritance is unsafe due to a previous AGX G15 fault. Stage129–133 developed controlled N1/N4 geometry A/B, compared repeated individual process GPU samples and added archive raster verification. **No new Stage131–133 Mac Studio GPU results were available when this step began.** GitHub Actions for the latest Stage133 HEAD `ddf0805796` were queued, not passed.

The owner previously had to manually create worktrees, configure and rebuild the fixture, run multiple Python/shell checks and launch 16 GPU processes, making a stale executable or mismatched capture possible. This stage is an **execution and provenance packaging improvement**, not a GPU-speed optimization.

## Changes and APIs

- `tools/batching-stage134-native-mac.sh` is a one-command native macOS launcher. It requires a Git worktree and Darwin, refuses tracked source/index modifications, and asserts the named Stage134 remote branch is an ancestor of the running HEAD. It validates build jobs 1–32 and absolute build/capture paths. Defaults: `build/batching-stage134`, `build/batching-stage134/captures`, eight parallel compile jobs; tunable with `AIVERSE_BATCH_BUILD_DIR`, `AIVERSE_BATCH_CAPTURE_ROOT`, `AIVERSE_BATCH_BUILD_JOBS`.
- The launcher records `native-provenance.txt`: Stage=134, exact `repo_head`, detached/named branch, `uname`, macOS version, Mac model, CMake and Apple Clang version, plus SHA-256 of the rebuilt `aiverse_material_batches` executable. It configures Release `-DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF`, builds the actual fixture, checks both shell scripts with `bash -n`, Python `py_compile`, Stage129/132/133 synthetic self-tests, then `--unit`.
- On success, invokes the **unchanged controlled 16-process geometry experiment** through `tools/batching-stage129-trials.sh`, forwarding the compiled binary, capture root and provenance path. It does not enable any production Metal path or alter the original Stage129/133 GPU parity requirements.
- `tools/batching-stage129-trials.sh`: when `AIVERSE_BATCH_NATIVE_PROVENANCE_FILE` is set, requires nonempty file, matching HEAD, valid 64-hex-character `binary_sha256`; copies it inside the capture and marks `native_provenance=native-provenance.txt` in manifest before executing GPU frames. Existing invocations **without this variable** still work. **Important correction:** an intermediate edit accidentally duplicated the trial-loop tail due to JavaScript replacement handling of a `$'` substring in a shell regex. Commit `4ca0abe` replaces that accidental content with the original Stage133 runner plus precisely the 13 provenance lines. GitHub compare after the correction reports 13 inserted lines and 0 deletions, not a duplicate runner.
- `tools/batching-stage133-verify.py`: optionally validates embedded `native-provenance.txt` against the capture SHA, Stage=134, nonempty OS/model/compiler and SHA-256 syntax. Its synthetic `--self-test` now also tests correct provenance and deliberate `repo_head` mismatch rejection, alongside previous corrupt MDI image rejection.
- `.github/workflows/gpu-batching.yml`: adds `bash -n tools/batching-stage134-native-mac.sh` to the existing macOS fixture preflight. The CI flow does not execute the full performance suite.

**No changes** to `GfxMetal.mm`, Metal ICB render commands, shaders, Vulkan/GLES, renderer policies, resource residency, buffer inheritance, GPU fences or defaults. CPU MDI / AUTO / inherited Metal pipeline remain opt-in/OFF.

## Actually run / not run

**Actually run in the assistant's Linux container:**
- `bash -n` on the locally authored Stage134 launcher (and a matching local draft with `sed` replacing `head -1` to avoid a possible `pipefail` SIGPIPE).
- Linux fail-closed check in a synthetic Git repo: launcher exited **2** with `Stage134 requires native macOS/Metal; no GPU pass claims on this host`.
- GitHub-source comparisons for Stage134: verified the corrected Stage129 trial runner has one `Stage129 archive:` endpoint and no repeated loop; the final change set before this handoff was precisely the new launcher plus three narrowly modified tool/CI files. Inspected the embedded provenance validation and synthetic self-test source for consistent fields.

**Not run:** checked-in GitHub CI on final Stage134 HEAD, native AppleClang compilation, C++ fixture `--unit`, Stage129/132/133 checked-in Python self-tests, 16 real GPU Metal processes, actual archive PPM parity, verified Mac model provenance, new GPU CPU timing or full CTest. The Linux check is **not a GPU test**. Stage134 is Draft until CI and owner-device evidence arrives.

## One-command Mac Studio acceptance

Create an isolated detached worktree; **never reset, checkout-overwrite or rebase** the owner's other AIVERSE, TerraWeave, Stage127–133, PR or unfinished merge worktrees.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage134"
git -C "$ROOT" fetch origin perf/batching-stage134-native-acceptance-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/batching-stage134-native-acceptance-20261120 || exit 1
cd "$WT" || exit 1
bash tools/batching-stage134-native-mac.sh
```

After native completion, share the exact `Stage129 archive: ...tar.gz` line (and preferably the archive), or the **first compiler/runtime failure**. Expected acceptance target (not yet measured): 16 successful independent processes; 1,056 valid whole-GPU-CB timestamps (66 each), 1,296 measured CPU frames (81 each), fixed 18 actual ICB submissions and 4,086 commands per MDI frame, pooled N1/N4 mesh uploads, original byte-identical raster within each run, and Stage127/129/132/133 reports plus native provenance in the archive. The Stage132 per-process reliability signal is descriptive with only two repeats, not statistical proof or isolated GPU model-pass timing. Do not claim performance improvement from this wrapper.

## Next agent

1. Query exact latest Stage134 HEAD GitHub CI results, inspect first error if failed and record actual logs. Don't infer passed CI from a queued job.
2. On owner native Mac Studio, execute the one-command acceptance and obtain real archive. If GPU crashes/PPM mismatches/pool rejection happen, preserve partial archive and stop; never weaken AGX guards.
3. Interpret Stage132 repeated Direct-vs-MDI premiums (GPU median/P95 and CPU wall) at N1/N4. Confirm whether the GPU premium is dependent on geometry workload. Only then choose a defensible Metal backend optimization, experimentally guarded if needed.
4. Cross-check representative textured/animated/transparent workloads and Metal/Vulkan/GLES correctness before enabling production AUTO/MDI. Use another separate stage handoff with full branch SHA, files, actual tests and next step.
