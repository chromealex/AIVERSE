# TerraWeave Stage70 QA — MetalDevice missing method terminator on integrated build

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Working branch: `integration/terraweave-physis-stage70-20261120`  
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## User's real Mac Studio failure

The user attempted the Stage70 merged native/editor/physics acceptance in
`~/Projects/AIVERSE-terraweave-stage70-local-qa`. At approximately
29–30% of the full build, AppleClang failed while compiling the shared
`aiverse_spectrafx_metal_rhi_test` target:

- `engine/gfx/metal/GfxMetal.mm:85`: `MetalDevice` unexpectedly abstract.
- `GfxMetal.mm:87`: initializer `layer_` does not name a member.
- Lines 88–97: `device_`, `layer_`, `queue_`,
  `frameSemaphore_`, `info_` reported undeclared.
- Clang stopped at 20 errors; `make: *** [all] Error 2`.

These were **actual reported failed native build diagnostics**, not a
Terra nutrient logic failure or an observed failure of CTest.

## Root cause

At `GfxMetal.mm` line 103, the method
`completedFrameSerial() const override` opened with `{` and returned
a ternary expression but never closed. The following declaration
`gpuSubmissionFence() const override` began immediately, with the
remainder of `MetalDevice` misparsed. This explains the apparently
unrelated missing members and abstract-class cascade.

Both `main` and the integration branch had the exact same original
Metal source blob
`0f6f443b86aeaf6cad72f2088ebf842ff51d5607`; the defect is
**shared renderer source** and must not be "fixed" by disabling
SpectraFX, Metal, the editor or the acceptance target.

## Exact fix and ownership

Source commit: `0aa3321f37d1af16be442ff96c274205c9ad4e93`.  
Modified path: `engine/gfx/metal/GfxMetal.mm` only.  
The sole diff is the missing `}` after
`completedFrameSerial()` and immediately before
`gpuSubmissionFence()`.

No changes to GPU lifetime, command-buffer waiting, fence counters,
`Device` virtual ABI, shader assets, native Terra/ECS interfaces,
`ForestDeadwoodResource` v3 archive, renderer batching or other
Metal methods.

GitHub commit diff and post-write readback confirmed the exact
one-line change and the balanced adjacent method boundaries.
**No compiler or CTest was run here**, because the actual Mac/Xcode
toolchain and full AIVERSE checkout are not present in this authoring
environment. Do NOT mark the build green until Mac native retry.

Only the Terra integration branch was modified. `main` still has
the original malformed source at the time of inspection; renderer
ownership should upstream this correction after independent acceptance,
not force-change `main` or overwrite other agents' branches.

## Minimal retry before expensive full acceptance

Run from the isolated QA checkout. If `git status --short` shows
any uncommitted user or agent work, resolve that separately; do not
reset/clean it.

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
cmake --build build/terraweave-main-acceptance \
  --target aiverse_spectrafx_metal_rhi_test --parallel 4
```

If the target or build directory is missing, configure first with the
**same** `AIVERSE_BUILD_TESTS=ON`, `AIVERSE_BUILD_EDITOR=ON`,
`AIVERSE_ENABLE_PHYSICS=ON`, `AIVERSE_FBX=OFF` flags used
in the acceptance script, then retry the focused target.

When the Metal target builds, run the **single full suite** on the
same source SHA (no feature switches or filtered PASS in lieu of full
acceptance):

```bash
AIVERSE_ACCEPTANCE_REQUIRED_HEAD="$(git rev-parse HEAD)" \
AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
  bash tools/terraweave-main-integration-acceptance.sh
```

The acceptance script records `provenance.txt`, checks the current
`origin/main` ancestor and required CTest registrations, and then
builds/runs all enabled targets with `ctest.xml`. The earlier verified
191/191 PASS on Mac was for historical SHA
`346dc66aeb0a89e1dc69ad889134fe7079ecb169`,
not this new integration commit.

## Exact QA status and next action

| Check | Status |
|---|---|
| Original Mac native build | **FAIL**, shared Metal parse error at ~29–30% |
| Source edit and GitHub diff/readback | **PASS**, one missing brace restored |
| Post-fix AppleClang Metal target | **NOT RUN** |
| Post-fix whole native/editor build | **NOT RUN** |
| Post-fix full unfiltered CTest | **NOT RUN** |
| NEREID/PHYSIS/Box3D sustained integration | **NOT RUN** |
| Metal screenshot/GPU parity, Vulkan/GLES runtime | **NOT RUN** |
| Main upstream correction | **NOT DONE**, other agents must be preserved |

Next: run the focused Metal native target and report the **first**
remaining compiler diagnostic, if any; then run the full
`terraweave-main-integration-acceptance.sh` and save exact SHA,
test inventory, counts and results. Do not advance Draft PR #53 or
Stage71 until a post-fix native acceptance result is available.
Record a **new standalone handoff** for any further changes.
