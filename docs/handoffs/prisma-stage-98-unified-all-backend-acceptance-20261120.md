# PRISMA Stage 98 — one-command integrated acceptance and real GPU stress/pixel fixtures

Date: 2026-11-20
Repository: chromealex/AIVERSE
Feature branch: `feature/prisma-main-acceptance-stage96-20261120` (Draft PR #84)
Starting source-tested baseline:
`9da0dcdbbbb5179f890a2ad740e7cb27eeb1ab8d` — user-run
**Release/complete CTest + Metal/GL33 Stage97 acceptance PASS**,
as documented in
`docs/handoffs/prisma-stage-97-full-native-metal-gl33-acceptance-pass-20261120.md`.
Previous branch documentation-only HEAD:
`a709af01e8dd1eb515c6ef065474390d150fac9a`.

## User requirement

The owner does **not** want to be asked to run the same verification one
small step at a time. Implement all remaining test coverage first, then
use exactly ONE comprehensive native command. Keep a single summary
with explicit PASS / FAIL / SKIP per backend and no invented test results.

## Source changes (Stage 98, after the verified Stage97 baseline)

1. `tests/render/HdrScene.h`: new separate opt-in real-device modes:
   - `AIVERSE_PRISMA_STRESS_VERIFY=1`: two 24-frame cycles of two
     simultaneously rendered view IDs, A-only and B-only camera cuts,
     B resize cycling among four resolutions, per-size shared cache
     eviction, and GPU-fenced retirement for both Prisma temporal
     and Renderer3D object-motion resources. Every cycle retires both
     views, waits via **actual `Device::gpuCompletedFence()`**
     (bounded to 120 extra empty host frames) for the last submitted
     fence, invokes the canonical nonblocking collection methods,
     and requires zero known pending textures/bytes, no remaining
     private histories/shared size caches, no active motion views,
     zero current PRISMA-owned known live bytes. Reports two
     separate drain markers and `PRISMA stress: PASS cycles=2...`.
     This is a CPU-visible RHI residency/accounting check after
     completed GPU work, not a driver-level physical VRAM query.
   - `AIVERSE_PRISMA_GHOST_VERIFY=1`: actual pixel readback on
     a red unlit cube moved from x=-2 to x=+2 after an explicit
     camera cut, using canonical temporal PRISMA. Capture old
     and new states on the actual backend, compute count/centroid
     of red pixels and bound residual color in the previous position.
     Async Metal callback data is protected by a mutex. The fixture
     verifies a bounded cut-specific ghosting scenario, not every
     skinned/transparent/disocclusion/subpixel image-quality mode.
   - Keep Stage97 `MULTIVIEW_VERIFY` separate, preserving previous
     verified behavior. The fixture rejects combined conflicting
     modes and requires explicit `TEMPORAL=1` and `JITTER=1`
     for each temporal test. Pixel tests remain self-contained
     asset-free and genre-independent.
2. `tests/render/hdr_scene.cpp`: when either long Stage98 mode is
   active, let the Metal platform runner advance up to 400 host
   frames, instead of the old 60-frame limit for HDR/TAA.
   `hdr_gl.mm` already supports 400 offscreen iterations; no change.
3. `tools/prisma/acceptance.sh`: extends the previously proven
   single Release configure + ALL build + full CTest + HDR/PPM/TAA/
   multiview workflow with four real hardware stages:
   `metal_stress`, `metal_ghost`, `gl33_stress`,
   `gl33_ghost` plus explicit proof markers; every stage logs
   subprocess exit and files, never short-circuits later GPU tests
   after a stage fails.
   Platform matrix explicitly reports native Vulkan, GLES, WebGL
   as SKIP on the unsupported Mac platform; on Linux it examines
   the built `aiverse_vulkan_tests` but distinguishes a driver
   absent/translation-only success from actual Vulkan device
   execution. GLES/WebGL need dedicated Android/EGL/browser
   runners; the Mac's GL33 driver is **not GLES or Vulkan**.
   An unavailable backend is not silently counted as a pass.

No new effects, engine runtime shader ABI changes, ECS simulation state,
serialized project schema or RHI device interfaces. Keep original
`main` untouched. Full tests and GPU fixtures are not artificially
skipped or marked as successful when unavailable.

## Important limitations

- Source-level GPU tests have been authored and GitHub-readback checked;
  **the new Stage98 code is NOT yet natively compiled or run on Mac**.
  A passing Stage97 SHA does not certify a later Stage98 SHA.
- The stress test only counts *known* Prisma-owned textures/history and
  Renderer3D object-motion handles. It cannot attest full system VRAM
  residency, Metal driver allocations, third-party caches or mobile budgets.
- The ghost fixture only validates a simple unlit rigid object after
  explicit camera cut. Extend later to reactive transparent edges,
  deformation, alpha cutouts and multi-camera moving object pixel parity.
- On an Apple Mac, Vulkan/GLES/WebGL are physically unavailable through
  the native default build; one-command local acceptance can inventory
  them as SKIP but cannot remotely prove their device behavior. Run an
  equivalent acceptance workflow on Linux/Android/browser runner for
  platform parity. Do not claim full three-backend parity from Mac-only
  success.
- Distinguish runner stage PASS count from CTest registered count.
  A `ctest_full PASS` and `Total Tests: N` registration do not by
  themselves show an exact per-test result table when raw log is unseen.

## Tests actually done in this source-authoring stage

- Inspected actual `Prisma::retireView`,
  `retireUnregisteredViews`, `retireUnreferencedSizeCaches`,
  `collectCompletedSizes`, `retirementStats`,
  `memoryStats` and Renderer3D object motion
  retirement/fence APIs. Verified `gpuCompletedFence` is the
  contiguous GPU completion token and the existing Stage97
  Metal/GL renderer frame ownership.
- Read back new GPU fixture code and acceptance script from GitHub.
  Static source checks: one Stage98 stress method, one pixel method,
  one configured mode each, two real-device backend stage pairs,
  zero obvious unmatched C++ braces, one script end-of-run status block.
- **NOT EXECUTED:** Bash syntax check (`bash -n`), native
  CMake/C++/Objective-C++ compilation, all-target link, CTest,
  Metal/GL33 Stage98 stress/pixel tests, Vulkan/GLES/WebGL
  backend runtime, real frame readback and performance.
  Treat these as NOT RUN until user or CI supplies actual output.

## Single owner-facing command (no incremental back-and-forth)

Use a **clean worktree**; don't overwrite another agent's changes.
From the previously established `AIVERSE-prisma-stage97` checkout:

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git status --short
git fetch origin
git switch --detach origin/feature/prisma-main-acceptance-stage96-20261120

bash -n tools/prisma/acceptance.sh &&
bash tools/prisma/acceptance.sh build/prisma-stage97
echo "PRISMA all-in-one exit=$?"
```

If the worktree is dirty, leave it unchanged and make an additional clean
worktree instead. The native runner now executes the entire Release
build, full CTest once, existing HDR/PPM/TAA/multiview fixtures plus
Stage98 stress/ghost on both Metal and GL33 in a **single invocation**.
No separate CMake target-by-target or manual standalone GPU launches
are needed.

All logs and captures:
`build/prisma-stage97/prisma-acceptance/summary.md`
and sibling `*.log`, `*.ppm`, `commit.txt`,
`git-status.txt`, `ctest-summary.txt`.

## Next

Read the FIRST failing real native compiler/test message *and all*
remaining failures from this same consolidated run before making fixes.
Apply narrow repairs in the feature branch with separate stage
handoffs; rerun a consolidated acceptance after fixes settle.
If all supported Mac stages are green, next platform acceptance is
Linux Vulkan/lavapipe and Android GLES/WebGL browser with actual
device/pixel evidence, not a Mac emulator. PR #84 remains Draft and
must not be merged to main until user approval.
