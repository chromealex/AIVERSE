# PRISMA Stage 97 — GL33 and Metal multiview hardware acceptance checkpoint

Date: 2026-11-20
Repository: chromealex/AIVERSE
Working branch: `feature/prisma-main-acceptance-stage96-20261120`
PR: https://github.com/chromealex/AIVERSE/pull/84 (Draft)
Previous: `docs/handoffs/prisma-stage-97-metal-multiview-hardware-pass-20261120.md`
User-reported **GL33 tested HEAD**: `cab4143f42f84d48ec04166b5247e0b2f995bd11`
Later remote HEAD includes documentation commits; no new runtime acceptance should
be inferred for that later SHA.

## Executed GL33 test (user's Mac Studio)

User ran the Stage97 `aiverse_hdr_gl` fixture from the
`AIVERSE-prisma-stage97` Mac worktree, following the supplied command:

```bash
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 \
AIVERSE_PRISMA_TEMPORAL=1 \
AIVERSE_PRISMA_JITTER=1 \
build/prisma-stage97/aiverse_hdr_gl
echo "GL33 exit=$?"
```

Exact user output:
```text
cab4143f42f84d48ec04166b5247e0b2f995bd11
GPU: GL33 Apple M3 Ultra / 4.1 Metal - 89.4; RGBA16F=1
PRISMA multiview: resize B 640x480 -> 512x384; fenced=16 quarantined=0
PRISMA multiview: retire B=1 sizes=1; fenced=19 quarantined=0 (not GPU-completed proof)
PRISMA multiview: PASS cameras=2 cut=1 resize=1 lifecycle=2 fenced=26 quarantined=0 completedFence=4
GL33 exit=0
```

The first line is the `git rev-parse HEAD` result provided in the same
terminal output. The explicit process exit is **0**. The GPU banner says
`GL33`: the `4.1 Metal - 89.4` substring is Apple's GL driver/version
string, **not** evidence this command used the native Metal backend.

The previously supplied user-run native Metal fixture returned:
```text
GPU: Metal Apple M3 Ultra; RGBA16F=1
PRISMA multiview: resize B 640x480 -> 512x384; fenced=2 quarantined=0
PRISMA multiview: retire B=1 sizes=1; fenced=6 quarantined=0 (not GPU-completed proof)
PRISMA multiview: PASS cameras=2 cut=1 resize=1 lifecycle=2 fenced=6 quarantined=0 completedFence=6
[metal-assets] libraries=0 sourceMisses=20 pipelines=22 pipelineFailures=0 archive=0 strict=0
```
There was no explicit `git rev-parse HEAD` and process exit in the earlier
Metal log, so record the reported Metal fixture marker as PASS without
claiming the exact binary Git SHA was established by that particular log.

## Actual result and scope

- **GL33 Stage97 fixture: PASS at the user-reported `cab4143f...` HEAD**,
  with explicit exit code 0.
- **Metal Stage97 fixture: PASS marker on physical Apple M3 Ultra**.
- Both exercised two stable view IDs with per-view temporal/velocity
  contexts, A-only cut, B-only size change and view/cache retirement
  checks; this is the exact scope of the authored fixture.
- GL33 `fenced=26` and `completedFence=4` are not interchangeable.
  `fenced` counts deferred retirement references, not proof of destroyed
  textures; `quarantined=0` does not prove no memory leak. The current
  fixture does not wait for all GPU operations and verify every retired
  resource has been reclaimed.
- GL33 fixture reports no pipeline count. Metal previously reported
  22 pipelines and 0 pipeline failures. Do not attribute Metal counters
  to the GL33 run.
- These smoke tests do **not** validate moving-object pixel ghosting,
  full temporal/upscaling image quality, Vulkan/GLES, complete CTest,
  release package, memory plateau or device loss/recovery.

## Source changes and agent handoff

Documentation-only checkpoint: this file, agent skill update, PR body.
No renderer, GPU shader, runtime implementation or CMake modification
for this result. The latest code changes remain those documented in
`docs/handoffs/prisma-stage-97-metal-device-scope-build-fix.md`.
The tests in the current committed branch were not executed by the agent.

## Next stage: one consolidated Stage97 full acceptance

Use the SAME clean user worktree and already-built cache. Since the
latest remote commits so far are documentation-only, an unnecessary
switch is not required merely to start the existing local script.
Record exact HEAD and do not mutate any dirty/concurrent worktree:

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git status --short
git rev-parse HEAD
bash -n tools/prisma/acceptance.sh
bash tools/prisma/acceptance.sh build/prisma-stage97
echo "PRISMA acceptance exit=$?"
```

The runner should perform one full Release build, one complete CTest,
native Metal and GL HDR probes, separate PPM captures, TAA, and both
multiview fixtures. Inspect
`build/prisma-stage97/prisma-acceptance/summary.md`, logs, actual
skipped/failing CTests, backend strings and captured images. This
overall run is **NOT YET RUN/REPORTED**, and should not be called PASS.

After this, Stage98 should focus on confirmed GPU fence draining /
resource residency plateau and actual per-view moving-object pixel
readback to measure ghosting. Vulkan and GLES remain separate
backend-specific build/runtime gates. Do not merge PR #84 into main
until consolidated results and the user's approval.
