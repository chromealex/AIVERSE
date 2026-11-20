# PRISMA Stage 98 — latest main reconciled for ONE full acceptance run

Date: 2026-11-20
Repository: chromealex/AIVERSE
Working branch: `feature/prisma-main-acceptance-stage96-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/84
Previous: `docs/handoffs/prisma-stage-98-unified-all-backend-acceptance-20261120.md`

## Scope

The owner explicitly asked to prepare ALL remaining tests before making the
next request for a native run. The previous Stage97 complete acceptance
finished PASS on `9da0dcdbbbb5179f890a2ad740e7cb27eeb1ab8d`.
Stage98 adds two explicit backend modes and one all-in-one native runner:

- `AIVERSE_PRISMA_STRESS_VERIFY=1`: two 24-frame two-camera churn cycles,
  cuts, changing output sizes, and **real GPU completed-fence** collection
  of PRISMA temporal target histories and Renderer3D object-motion
  resources. Zero remaining known pending resources after each confirmed
  GPU completion, with a bounded timeout. Does not claim physical
  system VRAM residency/driver allocation plateau.
- `AIVERSE_PRISMA_GHOST_VERIFY=1`: actual Metal/GL33 framebuffer
  readback from a rigid red object before/after a camera cut and
  cross-screen motion. Measures red-pixel count, centroid movement
  and residual coverage at its old screen position; not yet verified
  on hardware and not a guarantee for transparent/deformed motion.
- `tools/prisma/acceptance.sh`: ONE all-target Release build, ONE
  complete CTest, native Metal and GL33 HDR probes and P6 capture,
  native Metal TAA, Stage97 multiview, and Stage98 stress+ghost
  checks for both Metal and GL33, each with its own real subprocess
  exit/log and visible PASS/FAIL. If a test fails, later independent
  GPU tests still run (unless build itself failed).
- On Apple hardware Vulkan/GLES/WebGL default RHI tests are
  **not available**: the script reports explicit SKIP, not false PASS.
  Linux Vulkan test can be invoked when built and a real driver is
  present. GLES and browser runtime need their supported platforms.

## Upstream main advanced before next expensive run

Starting Stage98 feature HEAD:
`a07d23677e1446a0d6d0669536a0949ab5a85eaa`.

Latest upstream main:
`5aba82c35db0a8d998fb2b951914db11f3cfae0d`.

Main had advanced two commits, introducing Physical Sky and local
volumetric fog integration with PRISMA; one changed CMake registration
and 58 new HdrScene lines. Testing the old source would have left those
unverified. We performed a genuine non-forced TWO-PARENT merge:

**Merge commit**
`097bdf846f8531c743f70b29afd100358ad5367c`

Parents in order:
1. Stage98 feature HEAD
   `a07d23677e1446a0d6d0669536a0949ab5a85eaa`
2. Latest main
   `5aba82c35db0a8d998fb2b951914db11f3cfae0d`

- Preserved 30 latest-main changed added/modified file blobs,
  including PhysicalSky/LocalFog runtime, shaders, tests,
  CMake, documentation, readmes and AGENTS ownership.
- `engine/gfx/metal/GfxMetal.mm` already matched latest main
  byte-for-byte, including the earlier missing-brace repair.
- Conflict-aware `tests/render/HdrScene.h`: imported the
  `PhysisSkyBridge.h` include, Physical Sky/PHYSIS and Local Fog
  configuration, plus its existing animated-fog move fixture.
  Retained all Stage97 and Stage98 multiview, stress and ghost
  GPU test methods and dispatch. The combined HDR source has 788
  lines; +58 from latest main, with one Physical Sky mode and
  two references to the Local Fog test flag.
- No edits to main itself. A fast-forward with expected-head lease
  moved only the Stage98 feature branch ref.
- GitHub comparison after merge returned **ahead=35, behind=0**
  relative to main SHA `5aba82c35db0a8d998fb2b951914db11f3cfae0d`.
  Recheck if upstream advances again before any future merge.

## Actual performed verification

- GitHub API comparison of both branch lineages, verified exact
  two parent SHAs and feature ref fast-forward, no unmerged paths.
- GitHub readback of merged HDR source and exact merge commit.
- Static source checks: Stage98 `drawPrismaStress`,
  `drawPrismaGhostPixels`, Stage97 `drawPrismaMultiView`,
  new `AIVERSE_PHYSICAL_SKY_TEST`, `AIVERSE_LOCAL_FOG_TEST`
  and `PhysisSkyBridge` references all remain in the same file.
- The Stage98 acceptance script has one final exit condition,
  four new `metal/gl33_stress/ghost` test stages and explicit
  unsupported backend reporting.

**NOT RUN** on the new merge SHA: native compiler, CMake, CTest,
Metal/GL33 stress, visual/pixel capture, Physical Sky/Local Fog real
GPU fixture, Vulkan/GLES/WebGL hardware runtime or performance.
The previous 263 registered/full CTest PASS was on Stage97's
older SHA, not on this merge; do not transfer it.

## Exactly one consolidated Mac Studio run

On the existing `AIVERSE-prisma-stage97` worktree, do NOT reset away
uncommitted work. With a clean tree:

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git status --short
git fetch origin
git switch --detach origin/feature/prisma-main-acceptance-stage96-20261120

bash -n tools/prisma/acceptance.sh &&
bash tools/prisma/acceptance.sh build/prisma-stage97
echo "PRISMA all-in-one exit=$?"
```

The wrapper now compiles all current-main and Stage98 sources once,
runs full CTest once and reports all real device fixture outcomes.
Results, warnings, failures and skipped backends remain in
`build/prisma-stage97/prisma-acceptance/summary.md` and its logs.
Do not request separate manual isolated per-feature tests before
examining the COMPLETE report. Fix actual blockers in bounded source
commits + independent handoffs; rerun the one consolidated script
only after those fixes are ready.

Remaining non-Mac gates: independent native Vulkan (Linux/Windows),
GLES3 (Android/EGL), WebGL2 (Emscripten/Chromium); quality under
moving skinned/transparent/disoccluded pixels, production memory
residency and device loss/recovery. The test branch remains Draft,
main unchanged and never silently merged.

## Stage98 unified acceptance extension — complete latest-main sky/fog GPU variants

After the source merge, the latest-main Physical Sky and Local Fog
fixture variants must be included in the **same native command**, not
manually tested one by one.

Code commits:
- `e49298a3ddb5083597a86c72001836e4a62f355b`:
  `tools/prisma/acceptance.sh` adds a generic `scene_variant`
  runner which launches the **real Metal and GL33 HDR executables**
  with `AIVERSE_AUDIO=off`, one explicit sky or fog mode, a dedicated
  P6 capture and a per-case stage log.
- `c4b9ed1d15e5121d9306e672e51b7c57a144b492`:
  remove any previous PPM for that variant and require the current
  process to report exactly the expected capture path, plus backend
  marker and valid nonempty P6 header. A stale prior PPM cannot
  incorrectly make a failed/missing capture seem successful.

**Sky modes on each backend (11):**
`clouds`, `clear`, `medium`, `ortho`, `night`, `no-rays`,
`sunset`, `inside`, `above`, `physis-dry`, `physis-storm`.

**Fog modes on each backend (11):**
`off`, `sphere`, `reveal`, `move`, `obb`, `obb-reveal`,
`behind`, `inside`, `medium`, `ortho`, `combined`.

Total **44 new variant subprocesses** are executed by ONE runner command,
not by asking the owner to paste 44 commands. All outputs are kept
under `build/prisma-stage97/prisma-acceptance`.
This is real GPU fixture execution plus P6 capture validity,
**not** automatic high-quality visual pixel-difference certification.

These new sky/fog runner cases and Stage98 stress/ghost code have
**NOT** been native-compiled or GPU-executed on the new merge SHA.
Previous user-run Stage97 `9da0dcdb...` remains the only complete
hardware acceptance PASS. Do not claim full Stage98 PASS yet.
After fetching the latest branch, run exactly the previously documented
one-command full acceptance. Vulkan/GLES/WebGL need separate
native platform hardware/CI and remain explicit SKIP on macOS.
