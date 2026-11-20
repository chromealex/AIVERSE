# TerraWeave Stage70 — SpectraFX editor viewport retirement compilation fix

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Branch: `integration/terraweave-physis-stage70-20261120`  
Draft integration PR: https://github.com/chromealex/AIVERSE/pull/53

## Actual user-reported Mac build result

A fresh full native/editor build of the Stage70 integration advanced to **95%**, beyond the prior Metal parser blocker. The user then reported two **AppleClang errors**:

1. `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp:473`: `previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi))` passes three parameters, but `apps/editor/Editor.h:91` requires the additional non-defaulted `ViewportGpuRetirement& owner`.
2. `apps/editor/windows/SpectraFxGraphWindow.cpp:241`: equivalent obsolete three-parameter call, with `*device_`.

The warnings in `SpectraGraphWindow.cpp` about the unused `ed` parameter and `validId` function were warnings, **not** build-stopping errors. The previous one-brace Metal fix is documented in `qa-2026-11-20-stage70-metal-device-missing-brace-handoff.md`.

## Architecture and source modifications

`Editor.h` defines:

```cpp
void Viewport::ensure(gfx::Device&, int pw, int ph,
                      ViewportGpuRetirement& owner,
                      const char* debugName = "Viewport");
```

`Editor` exposes `viewportRetirement()` and `gpuDeviceLease()`. The current `SceneWindow.cpp` and `GameWindow.cpp` both call
`viewport.setGpuDeviceLease(ed.gpuDeviceLease())` then
`viewport.ensure(..., ed.viewportRetirement(), "...")`.
`Viewport::release` retires the old color/depth textures instead of immediately destroying potentially in-flight ImGui/GPU resources. The `Editor::shutdown` sequence clears windows before the GPU-retirement owner is drained.

**Code commit 1:** `0eabbfecce8a6d105aa45b4018f1de8c953275cc`  
Changed `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`. Before the GPU preview `ensure`, bind `ed.gpuDeviceLease()`, pass the editor's `viewportRetirement()`, and use debug name `"SpectraFX GPU Assets"`.

**Code commit 2:** `95ed023d919dc67b8a8a98e9db8e83301d6f8910`  
Changed `apps/editor/windows/SpectraFxGraphWindow.cpp` with the same owner/lease pairing and debug name `"SpectraFX Graph"`.

No changes to `Editor.h`, the GPU RHI interface, `ViewportGpuRetirement`, shared texture destruction semantics, SpectraFX simulation/graph format, TerraWeave, PHYSIS/NEREID, ECS or persistence v3. No silencing of compiler errors, new default owner, or disabled build targets.

## Actual verification

- **PASS — connected GitHub source inspection** of `Editor.h`, `ViewportGpuRetirement.h`, `Editor.cpp`, both SpectraFX files and existing Scene/Game examples.
- **PASS — GitHub writes and post-commit readback:** both updated files contain matching `setGpuDeviceLease(ed.gpuDeviceLease())` and `ensure(...,ed.viewportRetirement(), debugName)` calls; each source change is a narrowly-scoped call-site repair.
- **Observed before fix:** user Mac native build **FAIL** at 95% with these two exact compiler errors.
- **NOT RUN:** AppleClang rebuild after the two source commits, full native target build, full unfiltered CTest, GPU runtime/profiling, Vulkan/GLES, or simulated PHYSIS/NEREID/Terra.
- Historical Mac 191/191 PASS belongs to `346dc66aeb0a89e1dc69ad889134fe7079ecb169` and does **not** validate these new editor fixes.

## Next Mac Studio action

In the isolated checkout `~/Projects/AIVERSE-terraweave-stage70-local-qa`, confirm no uncommitted changes before moving detached HEAD (never reset or clean other agents' work):

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
cmake --build build/terraweave-main-acceptance \
  --target aiverse_editor --parallel 4
```

If the editor target builds cleanly, run the full unfiltered acceptance at the same exact SHA:

```bash
AIVERSE_ACCEPTANCE_REQUIRED_HEAD="$(git rev-parse HEAD)" \
AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
  bash tools/terraweave-main-integration-acceptance.sh
```

Collect the resulting `acceptance-reports/provenance.txt`, `build.log`, `inventory.log`, `required-tests.txt`, `ctest.log` and `ctest.xml`. Stop on the **first actual new** compiler failure; document another bounded fix/handoff. Do not claim post-fix native PASS or merge PR #53 until fresh evidence exists.

## Known limitations and follow-up

The two SpectraFX preview subsystems still have their own preview host lifecycle, unrelated to this API correction. Real resize/close/reopen/device-loss GPU-retirement behavior and native Metal graphics acceptance remain open. The warnings in `SpectraGraphWindow.cpp` are not addressed because they are not blocking. Integrate shared-editor source corrections upstream only after owner review and acceptance, without overwriting other agent branches.
