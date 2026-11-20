# PRISMA Stage 97 — Apple M3 Ultra Metal multiview GPU smoke PASS (user-run)

Date: 2026-11-20
Repository: chromealex/AIVERSE
Working branch: `feature/prisma-main-acceptance-stage96-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/84
Prior handoff: `docs/handoffs/prisma-stage-97-metal-device-scope-build-fix.md`
Remote branch HEAD before this documentation change: `cab4143f42f84d48ec04166b5247e0b2f995bd11`

## Actual hardware evidence supplied by user

The user executed `build/prisma-stage97/aiverse_hdr_scene` within the
`AIVERSE-prisma-stage97` Mac Studio worktree (the previous command used
`AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 AIVERSE_PRISMA_TEMPORAL=1`
`AIVERSE_PRISMA_JITTER=1`). The user pasted the following output:

```text
GPU: Metal Apple M3 Ultra; RGBA16F=1
PRISMA multiview: resize B 640x480 -> 512x384; fenced=2 quarantined=0
PRISMA multiview: retire B=1 sizes=1; fenced=6 quarantined=0 (not GPU-completed proof)
PRISMA multiview: PASS cameras=2 cut=1 resize=1 lifecycle=2 fenced=6 quarantined=0 completedFence=6
[metal-assets] libraries=0 sourceMisses=20 pipelines=22 pipelineFailures=0 archive=0 strict=0
```

The command returned to the terminal prompt. No explicit shell exit code
(`$?`) or `git rev-parse HEAD` was pasted; the exact **executed binary
commit** is therefore not cryptographically established. At observation
time, the GitHub branch HEAD was `cab4143f42f84d48ec04166b5247e0b2f995bd11`;
do **not** transfer this result to later branch revisions automatically.

## What is confirmed

- Native actual Apple Metal backend, Apple M3 Ultra, RGBA16F supported.
- New Stage97 multiview fixture reached its final `PASS cameras=2` marker.
- Two camera IDs independently exercised; A-only camera cut, B-only
  render-size change 640x480 -> 512x384; per-view/history lifecycle
  checks completed; B view and its size cache retired without dropping A.
- Real RHI/Renderer3D/Prisma pass path encoded, according to fixture
  checks and reported successful completion; no pipeline failure
  (`pipelineFailures=0`, `pipelines=22`).
- This is the **user's real hardware output**, not a run by the assistant.

## What is NOT established

- `fenced=6` is a pending retirement count; `completedFence=6` is
  the most recent reported GPU completion token. Neither proves all six
  pending resources are already physically destroyed, nor establishes
  zero VRAM leaks across an extended stress run.
- No moving-object screen pixels, history ghosting/quality comparisons,
  multi-view visual frames, resource residency stress, memory plateau
  or device-loss recovery were exercised by this Stage97 fixture.
- No GL33 multiview output has been supplied for this newest fixture yet.
- The full Stage96/97 acceptance script, complete native Release CTest,
  Vulkan/GLES/WebGL runtime, explicit shell exit code and exact tested
  Git SHA have not been reported for this result.
- The earlier Mac MetalDevice compile error was patched in branch source;
  this successful binary run confirms the runnable Metal path for the
  user's local checkout, but **not** all project targets or platforms.

## Source and documentation changes in this checkpoint

This checkpoint adds documentation only: this handoff, current PRISMA
agent skill and PR #84 status. No runtime, shader or CMake changes.
The existing source fixes are documented by the preceding handoff.

## Next precise action — check the GL33 equivalent (focused, no full CTest)

From the **same Mac Studio Stage97 worktree**:

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git rev-parse HEAD
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 \
AIVERSE_PRISMA_TEMPORAL=1 \
AIVERSE_PRISMA_JITTER=1 \
build/prisma-stage97/aiverse_hdr_gl
echo "GL33 multiview exit=$?"
```

Expected backend marker: `GPU: GL33 Apple M3 Ultra` (driver string can
vary); expected successful fixture marker:
`PRISMA multiview: PASS cameras=2 cut=1 resize=1 lifecycle=2`.

If GL33 fails, record first complete diagnostic, repair via small
commit plus handoff. Once both Metal and GL33 focused tests pass,
run one consolidated `bash tools/prisma/acceptance.sh build/prisma-stage97`
with exact SHA provenance and logs; inspect all CTest failures/skips.
Only after that, expand into true completed-fence/VRAM stress and real
per-view ghosting pixel validation. Keep PR #84 Draft and main unchanged
until verification and merge approval.
