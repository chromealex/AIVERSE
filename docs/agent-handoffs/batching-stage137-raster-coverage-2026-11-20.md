# AIVERSE — Stage 137: Metal ICB raster coverage at fixed geometry topology

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage137-raster-coverage-20261120`  
**Draft PR:** [#87](https://github.com/chromealex/AIVERSE/pull/87) targeting `perf/batching-stage136-native-results-20261120` (Draft PR #85; preceding batching PR chain #82 → #79 → #78 → #77 → #75 → #39). Do not merge directly into `main`.  
**Exact starting source commit:** `4c72d1dd26eb52598fb4907f921376c0dc19ae20`.  
**Implementation commits before this handoff:** `7b8ada8913d79c2909738ff1fab541a3783ea22c` (fixture), `c0c5f4338c086616be4cf4405a9e27189c48c5ab` (16-case runner), `c7280e313664e6ee99badc80b622a39ed49bfb83` (analyzer), `11e82403eac41c9703a794efeb62351dcb598dad` and `510c0f85e7867842eed47484e39a5f011f67f22e` (Mac launcher), `fcfc8e52b6faddedc0a47b11a2204abfdf5bdb44` (focused CI).

## Verified source basis and hypothesis

Stage136 inspected a real owner Mac Studio archive `capture-20261120T115651Z-43216.tar.gz`, exact benchmark HEAD `204b2a046306b99e9f8829f4a64a6e64ad3ffc36`. All 16/16 native processes completed with 1056 valid Metal whole-command-buffer timestamps and 1296 measured CPU frames. CPU MDI saved roughly 42–52% CPU render wall, but lost 18–40% whole-CB GPU time to Direct. N1→N4 geometry scaling increased absolute MDI GPU premium by about 0.076–0.103ms with 18 ICB submissions and 4086 commands per measured frame. This is evidence of a correlation, **not proof of a particular driver bottleneck**.

Reviewing the actual N4 Direct screen image from that archive showed a small gold sliver in the center, about **435 pixels with blue-channel intensity below 150** in the 960×640 PPM. Thus the earlier test's fragment footprint was extremely small relative to the viewport. Increasing tessellation from N1 to N4 changed vertex count and geometry work, but did not independently control projected triangle size. Stage137 tests a different falsifiable factor: projected coverage / raster / overdraw at **fixed vertex/index count and fixed draw topology**.

## Fixture design and changed code

### `tests/render/material_batches.cpp`

- Extended test-only `indirectFixture(..., meshSubdivisions, triangleScale=1.0f)` to apply a centroid-centered 2D position scale to every generated vertex. Default is exactly **1.0**, preserving existing fixtures and product output.
- Stress-only flag `AIVERSE_BATCH_DIAGNOSTIC_TRIANGLE_SCALE` strictly accepts the literal `1` or `4`; invalid values fail before initializing the graphics device. Other cases ignore the flag.
- Uses the same N=4 tessellation under both scales; triangle centroid (0,0.25,0) stays fixed. Per primitive: 16 authored triangles, 48 vertex slots, 48 indices. Across 2048 primitives: **32768 triangles, 98304 vertices and 98304 indices**. These counts are unchanged between scales. **131072 triangles was the superseded N=8 case**, not N=4.
- GPU upload remains 5,898,240 vertex bytes + 393,216 index bytes = **6,291,456 bytes** in the existing shared geometry pool (predicted from Stage136; the runner requires native confirmation). Node translations, material, shader, pass masks and GroupCap remain unchanged. Enlarging local triangle footprint is intended to increase covered pixels without increasing vertex/command count.
- Added GPU-free `--unit` checks that N=4 scale 1 vs scale 4 have identical total vertices, indices, mesh/node counts and per-primitive draw topology, fourfold triangle bounding-box width and height, and identical node translations. Logs `triangle coverage CPU checks=passed` when these checks pass.
- Emits `stress diagnostic triangle_scale=1|4` for each stress process.

### `tools/batching-stage137-trials.sh`

- New independent 16-process matrix: triangle scale 1 or 4 × inherited Metal ICB pipeline OFF/ON × forced Direct/MDI × repeats 1/2. Interleaves scales in alternating order for each Stage127 control to limit simple time-of-run/thermal confounding.
- Fixed `AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS=4`, 2048 authored model primitives, full shadow+opaque passes, no diagnostic group cap, default PBR material quality, explicitly enabled experimental CPU Metal MDI only in the subprocess. Leaves AUTO and GPU-authored ICB disabled.
- Strictly requires compiled new `--unit` topology check, 38 legacy CPU invariants, exact scale labels, **native N4 pooled allocation of 5898240+393216 bytes**, 66 whole completed GPU CB timestamps and 106 CPU timeline rows per process, 81 actual measured Direct/MDI frames, 18/4086 MDI submissions/commands on every measured MDI frame and no MDI submissions for Direct. Requires before/after raster parity and real frame-100 MDI capture parity.
- Runs the unchanged **Stage127 strict analyzer** on `scale1/` and `scale4/`; then runs Stage137 new coverage analyzer. Writes a manifest and a `Stage137 archive: ...tar.gz`, or a partial error archive. Restores no production feature flags and never modifies other agent worktrees.

### `tools/batching-stage137-analyze.py`

- Reads real `stage127-runs.csv` and `stage127-comparison.csv` from both scale folders and independently verifies 16 unique process identities, completed GPU-sample counts, actual Direct/MDI command topology, sane GPU median/P95/CPU medians and matching Stage127 aggregate timing values.
- Independently verifies exact P6 PPM dimensions/bytes, intra-run before/after pixel parity and genuine MDI frame-100 pixel parity for all 8 MDI processes, plus pooled allocation and mesh-subdivision labels.
- Compares one Direct OFF control image at scale1 to the same control at scale4. Requires **at least 32 different RGB pixels** to reject a change of mesh scale that produces no meaningful visible raster difference. This test is a minimal **image-difference control**, not an actual measured GPU fragment invocation count.
- Writes `stage137-coverage-comparison.csv`: four process-paired rows (OFF/ON × two independent repeat pairs) with GPU median, GPU P95, CPU median, MDI-minus-Direct premiums at both scales and the change in MDI premium when coverage grows. Reports whole-GPU-command-buffer values only. Two process repeats per cell cannot establish statistical significance.
- `--self-test` generates a deterministic synthetic 16-case fixture, checks a constant premium under the two scales, and verifies that corrupting one actual MDI-image byte causes hard failure. Uses Python standard library only. The locally executed identical source has verified Git blob SHA `31ec072b74e425062651a4fc86932d9a9c9fffa5` matching the GitHub blob.

### `tools/batching-stage137-native-mac.sh` and CI

- New one-command, Darwin-only launcher requiring a clean tracked-source worktree at the current fetched Stage137 branch, rebuilding `aiverse_material_batches` in its own Stage137 Release build directory. Captures macOS/mac model/compiler, exact HEAD and binary SHA-256 in the Stage137 capture archive, then executes shell and Python sanity checks, `--unit` and the complete native trial matrix.
- `.github/workflows/gpu-batching.yml`: extends the existing macOS CPU MDI job to syntax-check both Stage137 shell scripts, compile the new Python analyzer, run synthetic self-test, and execute existing fixture `--unit`. CI is a functional gate, **not** the owner-device performance benchmark.
- No changes to production Metal ICB buffer inheritance, AGX crash workarounds, RHI/renderer/shaders, Vulkan/GLES, AUTO batch admission/defaults or GPU resource fencing.

## Actual validation performed, versus pending

**Performed in this Stage137 response:**
- Reviewed source in the current GitHub branch, the Stage136 owner-native archive's PPM (gold sliver), actual runner/provenance contracts and the Stage127 CSV schema.
- Executed local `python3 -m py_compile` and `python3 batching-stage137-analyze.py --self-test` in the assistant container: **PASS: Stage137 fixed-topology raster scaling synthetic controls + MDI parity rejection**. Compared `git hash-object` of that locally executed analyzer source with GitHub's blob SHA: both `31ec072b74e425062651a4fc86932d9a9c9fffa5`.
- Independently simulated regular triangle tessellation with scales 1/4 in Python: N4 always yields 16 triangles per primitive, each silhouette area scales 0.225 → 3.6 in local model units (16× area), winding remains consistent. These are CPU geometry calculations, **not verified GPU fragment counts**.
- Used GitHub source fetch/compare for every changed-file anchor and maintained separate Stage137 branch/PR. The original Stage136 GPU numbers are from the earlier supplied archive, not from fresh Stage137 native captures.

**Not performed:** full native C++/AppleClang build after fixture edit, actual checked-in shell `bash -n` on GitHub-hosted checkout, Stage137 end-to-end macOS performance run or GPU timings, real N4 scale4 pixel parity, Metal validation-layer/runtime stability, full CTest, Vulkan/GLES compilation or real-game frame-rate comparison. These remain required acceptance gates. The new CPU `--unit` test is implemented but its native execution is not yet confirmed.

## Mac Studio execution — isolated fresh worktree

Do not change, reset, switch, or rebase existing owner, TerraWeave, Stage127–136 or other active worktrees.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage137"
git -C "$ROOT" fetch origin perf/batching-stage137-raster-coverage-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" \
  origin/perf/batching-stage137-raster-coverage-20261120 || exit 1
cd "$WT" || exit 1
bash tools/batching-stage137-native-mac.sh
```

The launcher compiles new test-only C++ and runs the remaining 16 GPU processes in independent subprocesses, each checking real Direct/MDI topology and byte parity. On success, share the `Stage137 archive: ...tar.gz` output **and the actual archive** so the next agent can independently recompute native GPU/CPU medians from raw CSV. On the first compiler error, ICB fault, output-image mismatch, unexpected pooled byte count, or coverage diff below 32 pixels, stop and retain the partial failure archive rather than weakening correctness gates.

Expected acceptance targets (NOT observed): 16/16 processes, 1056 valid whole-GPU-command-buffer timestamps, 1296 measured CPU frames, pooled N4 geometry in all processes, exact 18 ICB submissions/4086 commands on MDI frames, zero intra-process pixel differences, actual raster change across scales and `stage137-coverage-comparison.csv` written. If the experiment passes, compare the **absolute MDI GPU premium** at both coverage levels, with per-repeat Direct controls and P95. Whether the premium changes or remains constant is diagnostic, not proof of AGX driver cost or a mandate to enable MDI.

## Next agent

1. Check exact latest PR #87 HEAD and its GitHub focused checks; fix real compilation/shell/self-test failures with a separate handoff if found. Do not claim CI PASS while pending.
2. Read owner native Stage137 archive directly and independently validate data, topology, CPU/GPU timestamps, PPM parity and coverage difference; report repeat-level GPU premium and P95 plus CPU render wall, not just medians-of-medians.
3. If coverage strongly affects matched MDI GPU premium, investigate fragment/raster/overdraw interaction and depth state on Metal; if the premium stays constant, prioritize indexed ICB playback and per-command buffers. These are hypotheses, not proven GPU-counter results.
4. Add an opt-in production Metal optimization only after a supported root-cause finding and safety tests; preserve all AGX buffer-binding/residency/fence safeguards. Validate representative textured/mixed-material/transparent/skinned/LOD scenes and Vulkan/GLES architecture before considering AUTO default changes.
5. As required for every implementation stage, add another `docs/agent-handoffs/batching-stageXXX-*.md` with branch, full commit, changed APIs/files, tests actually run/results, limitations and next action.
