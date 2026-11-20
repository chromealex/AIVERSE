# AIVERSE — Stage 133: TerraWeave 271/271 native CTest PASS; provenance gate before main

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Documentation branch:** `integration/terraweave-physis-stage70-20261120`
**Owner's tested worktree:** `~/Projects/AIVERSE-terraweave-stage70-local-qa`
**Validation build directory:** `build/terraweave-main-acceptance`

## Actual Mac Studio results

Owner reports the local editor target rebuilt successfully after correcting SpectraFX preview `Viewport::ensure` API migration in:
- `apps/editor/windows/SpectraFxGraphWindow.cpp`: `previewViewport_.setGpuDeviceLease(ed.gpuDeviceLease())` then `ensure(..., ed.viewportRetirement(), "SpectraFX Graph")`.
- `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`: same with debug name `SpectraFX GPU Assets`.

Previously owner's native `spectrafx_metal_rhi` test passed 1/1 in 1.18s.

Owner subsequently ran full native CTest in `build/terraweave-main-acceptance`. Reported terminal tail:

```text
267/271 Test #265: fps_animation_actions .... Passed 1.06 sec
268/271 Test #107: aiverse_net_tests ........ Passed 105.39 sec
269/271 Test #199: skill_library_isolation .. Passed 256.73 sec
270/271 Test #110: aiverse_tests ............ Passed 847.88 sec
271/271 Test #188: place .................... Passed 25.60 sec
100% tests passed, 0 tests failed out of 271
```

Cumulative CTest label time figures (s*proc, parallel sums) include `Architecture=0.98` (4 tests), `ECS=1.73` (5), `EDITOR=6.45` (4), `MetalGPU=3.43` (4), `NETWORK=0.83` (2), `PERSISTENCE=1.71` (6), `PHYSICS=1.07` (2), `PHYSIS=13.73` (31), `RHI=2.23` (2), `Renderer3D=1.57` (1), `SpectraFX=11.17` (16), `TERRAWEAVE=0.49` (1). The complete test outcome is **271/271 PASS, 0 fail** on this **local** worktree. The owner also confirms successful editor target build; full all-target build likely completed because CTest followed it, but the full build's terminal output was not supplied in the current turn.

**Do not conflate** this with validation of Vulkan/GLES runtime backends on Mac, correctness of other branches, or the CPU MDI experimental pipeline flag (PR #39). Those remain separate.

## Connected GitHub branch inspection

Queried GitHub compare against current `main`:
- `integration/terraweave-physis-stage70-20261120`: `ahead_by=160`, `behind_by=0`, `status=ahead` at query time. Its complete two SpectraFX window file blobs exactly match the targeted owner hotfix branch versions, indicating these API migrations are already present in this integration branch.
- `integration/terraweave-main-20261120`: `ahead_by=102`, `behind_by=3157`, diverged.
- `feature/terraweave`: `ahead_by=91`, `behind_by=3624`, diverged.

These are GitHub **remote** relationships, not proof that the owner's detached local worktree HEAD is the first branch. In particular, the owner's tested source SHA and clean/dirty status are not yet known from the quoted 271-test log. Do not claim the remote branch's current commit passed 271 tests unless `git rev-parse HEAD` and status verify that it matches the tested source. The local two SpectraFX fixes may or may not be committed to the local HEAD.

## Validation and staged merge gate

**Actually done:** inspected owner's real Mac CTest summary; queried remote branch ancestry; wrote this handoff. **Not done:** agent-side native Mac execution, inspection of owner worktree Git status or exact HEAD SHA, remote CI confirmation, merge to main, Vulkan/GLES runtime tests, editor GUI smoke tests.

Before integrating, in the owner test checkout:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa || exit 1
git status --short --branch
git rev-parse HEAD
git diff --check
git diff -- apps/editor/windows/SpectraFxGraphWindow.cpp \
  apps/editor/windows/SpectraFxGpuAssetsWindow.cpp
git fetch origin main integration/terraweave-physis-stage70-20261120
git rev-list --left-right --count HEAD...origin/integration/terraweave-physis-stage70-20261120
```

Inspect output before attempting to merge. Do not run `git reset --hard`, force-push or overwrite another agent's changes. If local changes are uncommitted, preserve and commit them on the correct integration branch before a final remote merge; if HEAD differs, retest the precise candidate SHA and confirm its CI. If the 271-test checkout is identical to the remote candidate plus only the documented editor patch already in the remote candidate, reconcile and record this explicitly.

Only after provenance/CI confirmation should the team propose integrating the *right candidate* into main. CPU MDI PR #39 remains Draft and default MDI remains OFF.
