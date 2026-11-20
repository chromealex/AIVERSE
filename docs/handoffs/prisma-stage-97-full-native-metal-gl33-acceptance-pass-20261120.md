# PRISMA Stage 97 — full native Release + Metal/GL33 acceptance PASS

Date: 2026-11-20
Repository: chromealex/AIVERSE
Feature branch: `feature/prisma-main-acceptance-stage96-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/84
**Exact user-tested HEAD**: `9da0dcdbbbb5179f890a2ad740e7cb27eeb1ab8d`
Test worktree: `/Users/aleksandrfeer/Projects/AIVERSE-prisma-stage97`
Build directory: `build/prisma-stage97`
Previous: `docs/handoffs/prisma-stage-97-spectrafx-preview-viewport-owner-fix-20261120.md`

## Actual complete user-run native acceptance

User provided the tail of the real Mac Studio command and its generated
summary from `bash tools/prisma/acceptance.sh build/prisma-stage97`.
The runner's summary reports:

```text
SHA: 9da0dcdbbbb5179f890a2ad740e7cb27eeb1ab8d
Branch: detached
OS: Darwin
configure PASS
build PASS
registration PASS
count_guard PASS — 263 registered cases
ctest_full PASS
metal_hdr PASS
metal_hdr_evidence PASS — Metal; 54 GPU probes
metal_capture PASS
metal_capture_evidence PASS — real Metal P6 scene capture
metal_taa PASS
metal_taa_evidence PASS — real Metal stationary temporal probe
metal_multiview PASS
metal_multiview_evidence PASS — Metal two-camera lifecycle encoded
gl33_hdr PASS
gl33_hdr_evidence PASS — GL33; 54 GPU probes
gl33_capture PASS
gl33_capture_evidence PASS — real OpenGL P6 scene capture
gl33_multiview PASS
gl33_multiview_evidence PASS — GL33 two-camera lifecycle encoded
Failed: 0
Skipped: 0
PRISMA acceptance exit=0
```

The runner registered 263 CTest cases and successfully executed one
complete unfiltered CTest command (`ctest_full PASS`). Its summary is
not a raw line-by-line `ctest_full.log`, so report `263 registered +
full CTest PASS`, rather than inventing the exact internal per-case
pass/skip totals from an unseen raw test log.

The explicitly supplied GL33 tail additionally showed:

```text
HDR GPU probe 53: got 0.48456 expected 0.48453 PASS
[prisma] PASS gl33_hdr_evidence: GL33; 54 GPU probes
GPU: GL33 Apple M3 Ultra / 4.1 Metal - 89.4; RGBA16F=1
scene=HDR, post passes=11, ssr=1, reflection=1
capture: .../prisma-acceptance/gl33-hdr.ppm
[prisma] PASS gl33_capture_evidence: real OpenGL P6 scene capture
PRISMA multiview: resize B 640x480 -> 512x384; fenced=24 quarantined=0
PRISMA multiview: retire B=1 sizes=1; fenced=19 quarantined=0 (not GPU-completed proof)
PRISMA multiview: PASS cameras=2 cut=1 resize=1 lifecycle=2 fenced=18 quarantined=0 completedFence=5
[prisma] PASS gl33_multiview_evidence: GL33 two-camera lifecycle encoded
```

Metal fixture success and its own 54 probes/PPM/TAA/multiview are
grounded in the consolidated runner summary (only the GL33 tail was
pasted verbatim). The fixture validates a P6 image file's existence,
header and backend marker; no separate human visual review or
pixel-diff quality grade of the captured images was provided.

## What this closes

- Stage96/97 integrated Release CMake configure, complete editor/all-target
  build, unfiltered CTest and the runner's declared gates on this exact SHA.
- Previous `MetalDevice::completedFrameSerial` parser fix and the two
  `SpectraFX` editor preview `Viewport::ensure` lifetime-owner
  signature fixes are now included in a successfully compiled build.
- Actual Metal and GL33 HDR scene probes, P6 captures, Metal stationary
  temporal sampling and separate real-backend dual-view cut/resize/
  history/retirement lifecycle fixture all report PASS.
- No failed or skipped steps in the Stage97 acceptance **script**.
  The script's own skipped counter is not proof every optional backend
  or platform exists.

## NOT proven / separate Stage98+ work

- Vulkan, GLES, WebGL hardware-runtime parity: explicitly NOT VERIFIED
  by the Stage97 script.
- These per-view GPU tests are encoding and CPU/RHI-lifetime diagnostics.
  The counts `fenced=...` and `completedFence=...` are different
  measurements; the tests do not yet prove every pending target was
  subsequently destroyed, no memory leaks under repeated churn,
  memory plateau, or completed-fence resource reuse.
- No moving-actor temporal ghosting/silhouette error pixel comparison,
  multi-camera image-diff evidence, high-load GPU memory stress,
  device loss/recovery, mobile performance or all 19 PRISMA effects'
  production visual parity. Do not interpret 54 HDR probes as that.
- Source changes in this handoff are documentation only. This report
  applies to the exact user-tested HEAD above, not to later doc-only
  or functional commits without fresh acceptance.

## Source / architecture references

Canonical renderer: `engine/render/Prisma.{h,cpp}`, `Renderer3D.cpp`.
Stage97 real-backend multiview fixture:
`tests/render/HdrScene.h`; renderer front ends:
`tests/render/hdr_scene.cpp` (Metal), `tests/render/hdr_gl.mm` (GL33).
Consolidated acceptance: `tools/prisma/acceptance.sh`.
Editor retirement owner: `apps/editor/Editor.h`,
`ViewportGpuRetirement`; preview windows fixed:
`apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` and
`apps/editor/windows/SpectraFxGraphWindow.cpp`.
No production runtime/API files changed in this documentation checkpoint.

## Exact next agent action

Stage98 should first add a bounded test for confirmed GPU-completed
retirement and resource/memory plateau over repeated camera open/cut/
resize/close cycles. Unlike Stage97, require the real backend to report
completed GPU submission fences before asserting pending resources
are actually collected; do not infer completion from a CPU frame count.

Then add deterministic moving-object color/depth/motion/reactive GPU
readback samples that distinguish first frame, camera cut, disocclusion,
transparent silhouettes, and two view IDs; report measured pixel
differences with conservative tolerances per backend. Validate
Vulkan and GLES compile/runtime independently on supported hosts.
Keep PR #84 Draft unless the owner explicitly requests merge; main
and unrelated agent branches remain untouched.
