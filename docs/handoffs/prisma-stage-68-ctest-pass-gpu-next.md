# PRISMA Stage 68 — Mac full CTest PASS, GPU validation pending

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-67-validation-fixes.md
Main and PR base unchanged.

## Actual user-reported Mac evidence

The user supplied these exact lines after pulling the Stage-67 fixes and running
the consolidated native CTest:
```
100% tests passed, 0 tests failed out of 115
Label Time Summary:
SpectraFX = 0.25 sec*proc (6 tests)
```
This is a legitimate report from the user's Mac, not a test executed by this
agent. 115/115 CTests PASS, including the previously failed prisma_volume,
hdr_policy and skill_audit cases. The supplied excerpt has no explicit build
command/exit status or GPU capture output; do not infer GPU correctness.

## Source changes in Stage 68

No runtime, shader, GPU/RHI, ECS or test code changes. This is a documentation
checkpoint only. Stage-67 fixes were previously committed and verified by
full user-run CTest. No merge or force push.

## Existing GPU smoke fixture from source

The existing .github/workflows/prisma-final-validation.yml explicitly invokes
`build/prisma-final/aiverse_hdr_gl` on macOS if the binary exists, setting:
- `AIVERSE_HDR_VERIFY=1`
- `AIVERSE_HDR_CAPTURE=<absolute output PPM path>`
The workflow runs `aiverse_hdr_scene` via xvfb-run on Linux, only if built.
Absence of executable must be reported NOT RUN, not PASS.
On an actual Apple Silicon Mac this is at least a native fixture check, but
the log must identify backend/device before calling it Metal coverage.

## Exact next action on the user's existing Mac worktree

```sh
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
if [ -x build/prisma-final/aiverse_hdr_gl ]; then
  mkdir -p build/prisma-final/prisma-gpu
  AIVERSE_HDR_VERIFY=1 \
  AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/hdr.ppm" \
    build/prisma-final/aiverse_hdr_gl \
    2>&1 | tee build/prisma-final/prisma-gpu/hdr-gl.log
else
  echo "NOT RUN: aiverse_hdr_gl fixture missing"
fi
```
Collect native output and PPM, check GPU device/backend, frame readback, HDR
policy, compile failure and return code. Be careful: the pipeline command
`... | tee ...` needs `set -o pipefail` for the shell exit status to
represent the fixture status.

Additional acceptance needs genuine Metal renderer shader compilation and
GPU visual capture, multi-view history/retirement/fence stress, render graph
shader graph (TEX8/TEX9), temporal jitter/cut, DoF, alpha-reactive motion,
as well as Vulkan/GLES on hosts where those backends are supported. An OpenGL
fixture on macOS is not equivalent to Metal validation.

## Validation state
- Native CTest: PASS 115/115, reported by user on Mac.
- Earlier source readback: PASS, documented in Stage 67.
- Metal GPU rendering: NOT CONFIRMED.
- OpenGL HDR fixture: NOT RUN/NOT REPORTED in current user result.
- Vulkan/GLES: NOT RUN/NOT REPORTED.
- GPU fence, pixel parity and perf: NOT RUN/NOT REPORTED.

After any new actual GPU results, append them to a new handoff with precise
commands, backend/device and exit codes, before advancing the stage.
