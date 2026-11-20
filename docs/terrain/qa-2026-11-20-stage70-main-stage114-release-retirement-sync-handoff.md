# TerraWeave Stage70 — current main Stage114 Release retirement integration

Date: 2026-11-20
Repo: `chromealex/AIVERSE`
Branch: `integration/terraweave-physis-stage70-20261120`
PR #53: https://github.com/chromealex/AIVERSE/pull/53 (Draft)

## Preconditions and independently verified QA

The prior focused QA ran on exact `b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`.
Mac Studio reported:
- Stage70 focused native runtime runner final PASS (five selected targeted CTests).
- Real Metal GPU `spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
  `spectrafx_metal_rhi`, `spectrafx_metal_scene`: 4/4 PASS,
  0 SKIP, 0 FAIL/UNKNOWN.
- Real P6 capture/strict analysis: 5/5 PASS, changed A/B/PRISMA
  192/322/232 pixels.
- The previous full native/editor/Box3D acceptance 271/271 PASS
  belongs to older exact `b17e775a`; it is not a new postmerge result.

Result and evidence context are in
`docs/terrain/qa-2026-11-20-stage70-strict-metal-runtime-green-handoff.md`,
committed in Terra parent `2d9446dc5a5027708b1bbe8a4202dc26c139d1c0`.

## Main movement and exact true merge

Main advanced from `d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`
to `ad175f624ef6f25e7c6dc19cabe199cb30623201`
with Stage114 fix via PR #76 (two commits total).
This is *only* Release/GCC assertion semantics for two existing
CPU-resource-retirement regression tests; no production shader,
GPU source, TerraWeave or ECS changes.

Connected GitHub compare from old main `d6c8fa` to new main
listed exactly three files:
- `tests/render/async_resource_retirement.cpp`
- `tests/render/resource_retirement.cpp`
- `docs/handoffs/aiverse-stage-114-release-retirement-tests.md`.

A second compare from old main to Terra parent `2d9446dc` showed
**none** of these three paths were changed by Terra. The merge therefore
preserved every Terra tree entry and copied the exact three upstream
Git blob SHAs, not approximate hand-edited text:
- `043f378f0d19e74047d388c4b6f8f19b202894f1` (async test)
- `c6066baac88365ac47460d6a3bddf41a67259102` (retirement test)
- `58c7aac992d033db824dbbca0a0fa1051b019aee` (Stage114 handoff).

**Created genuine two-parent merge commit**
`25828ffce44f27d449f15f3cf992e2ee736fa2e8`:
- First parent: Terra documentation/QA head
  `2d9446dc5a5027708b1bbe8a4202dc26c139d1c0`.
- Second parent: current main
  `ad175f624ef6f25e7c6dc19cabe199cb30623201`.
- Merged tree: `ab0c3d00a13fe0c664b889172f1540628cc14a8d`.
- Updated **only** the Terra integration branch using an
  expected-head protected **non-forced fast-forward**.
  `main` and `feature/terraweave` were not modified.

Post-update GitHub readback verified merge HEAD, main ancestry,
`main...integration` **ahead=168 / behind=0** and diff from Terra
parent exactly the expected three files. PR #53 remains Draft.

## Test status (separate premerge vs postmerge)

**DONE / verified via connected GitHub:** ancestry, commit tree,
two parents, correct upstream blobs, changed-path diff,
expected-head protected push.

**NOT RUN on merge `25828ffc`:** C++ compile, Release checks
on GCC/AppleClang, full CTest, Metal GPU render, forest visual,
Vulkan/GLES, sustained authentic four-owner erosion. The earlier
271/271 and real 4/4 Metal PASS remain valid evidence for their exact
premerge SHAs; source changes in two tests still require targeted
Release checks before promoting the new merged head.

## Minimal native test commands for Mac / Linux (new main-only files)

Run on a clean isolated QA checkout; do not reset/clean other worktrees.
The previously accepted Mac focused Metal/Stage70 results do not need
a repeat simply because these **test-only** source files changed.

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD

cmake -S . -B build/terraweave-retirement-release \
  -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
  -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_ENABLE_PHYSICS=OFF
cmake --build build/terraweave-retirement-release --parallel 4 \
  --target aiverse_resource_retirement_tests \
           aiverse_async_resource_retirement_tests
ctest --test-dir build/terraweave-retirement-release \
  -R '^(resource_retirement_queue|async_resource_retirement_queue)$' \
  --no-tests=error --output-on-failure
```

If this configure fails because the host does not support some
unrelated optional target settings, document the exact diagnostic,
not a made-up PASS, and retain the source untouched until diagnosed.
Linux ARM64 GCC `-Werror` parity should be covered by upstream
CI; AppleClang Release does not prove that GCC-specific warning
regression is closed.

## Remaining Stage70 acceptance

1. Review and preserve the already-produced real Metal PPM/JSON
   artifact; separate forest GPU visual LOD, alpha cutout, viewport
   resize and GPU retirement under real load are not yet established.
2. Validate actual Vulkan/GLES rendering on supported hosts.
3. Add and execute a prolonged **authentic** PHYSIS × NEREID ×
   TerraWeave × Box3D single-world erosion, root/fall/replay scenario.
4. Complete the relevant CI for the postmerge SHA; workflows queued
   or cancelled are not passing evidence.
5. Keep PR #53 Draft pending explicit release scope decision.

No change to Stage70 v3 archive or world simulation behavior.
Next agent must record exact SHA, real test outputs and independent
handoff for any further bounded stage.
