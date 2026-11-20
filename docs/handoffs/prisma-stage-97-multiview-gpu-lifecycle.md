# PRISMA Stage 97 — actual-backend multi-camera temporal and retirement smoke

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: `feature/prisma-main-acceptance-stage96-20261120` (Draft PR #84)
Starting branch SHA: `0c3854b20dc52917d55d7a468d96ef71099128b7`
Starting integrated main: `f9aed9a705982e2c6b77b29869e309995ce8b163`
Previous: `docs/handoffs/prisma-stage-96-main-acceptance-runner.md`

## Scope and implementation

Stage 97 extends the existing asset-free native HDR fixture; it does not
introduce another postprocessor or change the production Prisma/Renderer3D
API. It is an explicit opt-in GPU smoke and lifetime regression, **not**
a proof of visual anti-ghosting or actual backend fence completion.

1. `tests/render/HdrScene.h`:
   - New `AIVERSE_PRISMA_MULTIVIEW_VERIFY` fixture mode; demands
     `AIVERSE_PRISMA_TEMPORAL=1` and `AIVERSE_PRISMA_JITTER=1`,
     conflicts with HDR_VERIFY/legacy TAA/ScreenLighting fixture modes.
   - The Stage97 profile is set **after** existing HdrScene baseline
     `post.configure()` and `defaultQualityFor()`, so it cannot be
     unintentionally replaced by the original fixture setup.
   - Two stable IDs, 1001 and 2002, render through REAL Renderer3D +
     canonical `Prisma::resolve` in the same host frame, initially
     equal size (640x480). Both must have independent per-camera
     effect reports, sequence counters and TWO owned temporal
     RGBA16F history targets. They share one size-keyed cache.
   - On tick 3, an explicit cut resets only camera 1001 in Prisma
     AND Renderer3D. Verify `objectMotionEpoch` changes for A but not B.
   - On tick 4, resize B to 512x384; require new B temporal-history
     dimensions while A's history remains 640x480. Record fenced vs
     quarantined texture retirement counts.
   - On tick 5, render A only, retire unregistered B and only the
     unreferenced size-keyed cache; check A remains valid.
   - On tick 6, retire all remaining views, call the normal GPU
     retirement collector (nonblocking), verify active camera and
     size-cache state is gone, report actual completed GPU fence.
   - **Never** treat a pending/fenced texture count as proof its memory
     was physically freed. Renderer3D itself manages separate object
     motion resources, which this Stage97 fixture does not exhaustively
     assert retired. The GPU passes are encoded on the real backend
     but do not perform per-view pixel readback comparisons.

2. `tools/prisma/acceptance.sh`:
   - After the Stage96 Release full-build/full-CTest and existing HDR
     probes/PPM capture/TAA, add separate `metal_multiview` and
     `gl33_multiview` stages with both temporal opt-in flags.
   - Check process exit status AND explicit `GPU: Metal`/
     `GPU: GL33` backend marker AND final
     `PRISMA multiview: PASS cameras=2` marker.
   - On other operating systems / no GPU executables, record honest
     SKIP, or FAIL when `AIVERSE_PRISMA_REQUIRE_GPU=1`.
   - Do not change CMake: both `aiverse_hdr_scene` and `aiverse_hdr_gl`
     already compile `HdrScene.h` for the production backends.

No changes to ECS, game state, serialized `postfx`, shader resource slots,
Vulkan/GLES backend sources, or other agents' branches.

## Commits and files

- `56989d1bb18b4b96f2dad85fd7ba6626a4d38da0`: Stage97
  `HdrScene.h` new multi-view GPU lifecycle exercise.
- `05fdb0c250ef29aeefedffc08d7f7533e8390e3e`: ensure
  temporal profile isn't overwritten by fixture default setup.
- `6fd872a9e51f7f8075bddf69c924782c9e4e6363`: wire
  real Metal/GL fixture stages into Stage96 acceptance script.
- This document and the companion agent-skill update form the
  separate mandatory Stage97 handoff.

## Validation performed vs pending

Performed:
- Inspected production `Prisma::renderSizeForOutput`,
  `jitteredCamera`, `resolve`, `effectReport`,
  `memoryStats`, `retireUnregisteredViews`,
  `retireUnreferencedSizeCaches`, `collectGpuRetirements`
  and Renderer3D render/resolve APIs before writing tests.
- GitHub readback of exact updated files. Static source checks
  passed: one multiview method, profile setup AFTER baseline,
  both backend execution gates, cut/resize/retirement assertions.
- GitHub comparison to base will be recorded after skill/handoff.

**NOT EXECUTED:** native clang/ObjC++ compile, `bash -n`, CMake configure,
CTest, GPU Metal/GL tests or screenshot pixel parity. The assistant does
not have the user's Mac Studio checkout or Metal hardware; container
`curl` cannot resolve GitHub DNS. No new PASS claim should be inferred
from source presence.

Previous user-confirmed 115/115 CTest and HDR GPU probes belong to
older pre-merge revisions, not this Stage97 branch.

## Exact next actions for the Mac Studio

```bash
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-stage97 origin/feature/prisma-main-acceptance-stage96-20261120
cd ../AIVERSE-prisma-stage97

# Fast syntax gate before the long native run
bash -n tools/prisma/acceptance.sh

# Build the two fixture targets FIRST to catch source errors quickly
cmake -S . -B build/prisma-stage97 -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-stage97 --parallel 4 \
  --target aiverse_hdr_scene aiverse_hdr_gl

# Targeted actual hardware smoke BEFORE the one consolidated full suite:
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 AIVERSE_PRISMA_TEMPORAL=1 \
  AIVERSE_PRISMA_JITTER=1 build/prisma-stage97/aiverse_hdr_scene
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 AIVERSE_PRISMA_TEMPORAL=1 \
  AIVERSE_PRISMA_JITTER=1 build/prisma-stage97/aiverse_hdr_gl
```

If the smoke fails, capture the exact first failure and fix the regression
without weakening assertions or hiding device skips. After source settles,
`bash tools/prisma/acceptance.sh build/prisma-stage97` runs the complete
full suite **once** with all GPU stages and preserved evidence.

Then Stage98: true per-view GPU pixels (movement/ghosting), frequent
cut/resize stress, verified completed fences and resource residency,
and separate Vulkan/GLES compilation and runtime fixtures. Draft PR #84
must stay unmerged until the native gate is reviewed.

## Stage 97 final integration adjustment and agent-skill sync

- `4068788c0ad48809eab7f4f504b88d6469c4a23f`:
  `tools/prisma/acceptance.sh` now uses
  `AIVERSE_PRISMA_CTEST_TIMEOUT` (validated 1–7200 seconds,
  default 1800), since some genuine current AIVERSE full native
  tests have exceeded the old 900-second per-case limit.
  This avoids a false-red timeout without skipping or relaxing tests.
- `b016e0853cf0741796c58c8fad0bf6ef85a9a02e`:
  `skills/visuals/aiverse-prisma/SKILL.md` points to the Stage97
  handoff, current backend fixture flags, correct GPU-fence semantics
  and native validation limitations.
- No changes to Vulkan/GLES or production shader paths. This
  remains a source-only authored test extension; all native outcomes
  are still **NOT RUN** in this stage.
