# Texture RHI — indexed Metal pixel acceptance, 2026-11-20

Branch: `codex/texture-completion`, based on main merge
`7a8947e35d6baa595461799d75046a7ab3d5d75a` (PR #86).
Implementation: `ed1a427a61960b02a658e1c46f29dc1ac4261ee7`.

## Changes and compatibility

Extended `tests/render/metal_vt_bilinear_gpu.mm` with 18 native GPU
closed-form color checks for the production indexed trilinear and anisotropic
MSL functions; documented scope in `docs/texture-rhi.md`. Existing 136
nonuniform gutter/bilinear probes are preserved. Checks cover physical slice
permutation, layer stride, virtual LOD blending/clamping, missing/invalid
entries, addressing, 1/2/4/8 taps, mip bias and Y-major derivatives.
No runtime code, public API, save format, default feature flag or ICB policy
changed. No full Renderer3D indexed sampling or performance certification.

## Actual verification

All new build outputs, module cache, fixture temporary files and reports are
on KINGSTON. Source worktree is local at
`/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/texture-completion`.
For both configure/build and test execution the environment was:

```
TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug/tmp
CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug/module-cache
```

Commands actually executed from that source worktree:

```
cmake -S . -B /Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON
cmake --build /Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug --target aiverse_metal_vt_bilinear_gpu_test -j 4
bash run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug --filter '^metal_vt_bilinear_gpu$' --ctest-jobs 1
git diff --check
```

All PASS. Native Apple M3 Ultra: 1/1 selected CTest, 7.28 seconds;
18 indexed probes and 136 legacy bilinear probes (120 seam samples,
legacy maximum channel delta 1), no GPU errors. The build emitted no
compiler warnings/errors. Configure/build logs live under the external Debug
root; test report is `test-reports/20261120-141642-52152/` and detailed
sample counts are in `Testing/Temporary/LastTest.log` there. The test ran
on the exact code later committed above; only documentation was appended
afterward. Full engine regression NOT RUN for this test-only extension.

Prior PR #86 focused CI remains queued, not passed, as checked this stage:
https://github.com/chromealex/AIVERSE/actions/runs/44750616476
(all four Linux/OpenEXR, GLES3 EGL and Vulkan jobs queued).
This branch CI/merge is pending publication at handoff creation.

## Next actionable step and limits

Inspect focused CI when runners start. Next local acceptance gap is a
nonuniform indexed virtual-mip/anisotropic CPU image oracle including
physical mip-chain filtering and streamed fallback transitions. Then test
indexed material sampling through Renderer3D/ICB. Current solid-color
probes establish selection and mixing, not nonuniform filtering quality.
GL/Vulkan runtime parity, streaming stress and GPU timing remain unverified.
All further builds, caches, logs and temporary fixtures must stay on KINGSTON;
never silently fall back to an internal disk if it is unavailable.
