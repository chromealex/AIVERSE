# TerraWeave Stage70 — strict Linux Vulkan + GLES3 forest renderer QA

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `integration/terraweave-physis-stage70-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Preceding accepted native evidence

Exact Mac Studio forest source
`0133eb21b211af62d205952db77c50589ae55a13`
passed the **real Metal GPU** `terraweave_metal_forest` 1/1 CTest
(2.43s, no failures). Source model BranchLOD changed 6480 pixels,
masked impostor 3891, cutout holes 7729, resized RT 1502; distinct
impostor azimuth UVs, six actual GPU PPM frames and a successful
GPU-idle release. See
`qa-2026-11-20-stage70-metal-forest-hardware-pass-handoff.md`.

Earlier accepted full Mac native/editor/Box3D CTest 271/271 PASS
belongs to exact `b17e775a`; separate strict SpectraFX Metal
GPU 4/4 and PPM 5/5 PASS belong to `b1bb27c8`. None of those
results proves Linux Vulkan/GLES3.

## This bounded new QA-source stage

1. `48414952834c6484cefeba66e0294edd99c8d645` — create
   `tests/terraweave_linux_forest_backends.cpp`.
   This runs actual `ArborForestAdapter` model upload/draw/release,
   `Renderer3D`, a signed CPU-only foliage placement and real
   backend `gfx::Device::captureNextFrame`.
   It has two explicit modes:
   - Vulkan: production `gfx::createVulkanDevice(VulkanWindow{})`
     offscreen driver, with **no** skip-on-missing-driver behavior.
   - GLES3: create a real EGL ES3 pbuffer surface/context before
     production `gfx::createGLDevice(Backend::GLES3)`.
   Exact backend identity is checked to forbid fallback.
   Both capture baseline, near BranchLOD and far alpha-masked impostor
   and fail if frames are absent or visible changed pixels <=60.
   The GPU retirement barrier must succeed before releasing models.
   The final GPU device is destroyed before the EGL owner/context.
   PlatformLinux's existing `main()` forwards to test `aiverseMain`.
2. `75e370672416c5e4f0e395c89f5075c1ff8b8fa1` —
   `CMakeLists.txt` registers the Linux-only executable
   `aiverse_terraweave_linux_forest_backends`, CTests
   `terraweave_vulkan_forest` and `terraweave_gles3_forest`
   when `AIVERSE_VULKAN=ON`. It links the existing native
   runtime, models, world, zstd, Render2D, EGL and GLESv2 libraries.
3. `bf01d9559f4c6df4f5ce6993dfc2b854f9ce6d29` —
   `tools/terraweave-linux-gpu-acceptance.sh` builds the focused
   executable in an isolated Linux build directory, validates exact
   Git SHA, clean checkout and Vulkan CMake ON, checks both CTests
   are registered, runs Vulkan directly and GLES3 under Xvfb.
   Both must complete an actual driver frame and emit three nonempty
   P6 image files each. CTest, compiler and script failures remain
   nonzero; no faux skipped GPU PASS.
4. `15a1fbeef5d00e731ba47f330d56580e3a059619` —
   `.github/workflows/terraweave-linux-gpu.yml` adds a
   standalone Ubuntu-24.04 software-driver QA (Mesa Vulkan lavapipe
   and EGL/GLES llvmpipe, Xvfb). It can be dispatched manually or
   triggered on specific source/CMake changes, not on every future
   documentation-only push. Preserve full logs and real PPM artifacts.

**What did not change:** production gameplay code, authoritative
PHYSIS weather, NEREID water, TerraWeave ecology/nutrients, v3
`ForestDeadwoodResource` binary archive, editor UI, Metal forest
render path, GLSL/Metal shaders or public native ABI. The goal is
cross-backend testing of an *existing* shared renderer, not a
duplicate Vulkan/GLES forest system.

## Actual QA status

**DONE:** reviewed via connected GitHub source the existing Linux
`PlatformLinux.cpp` backend selection, EGL/GLES context, Vulkan
offscreen test, `GfxGL.cpp` and Vulkan capture/idle API,
CMake linkage model and TerraWeave production ForestAdapter.
Created the four source/workflow changes above; fetched them
back by exact branch path and confirmed their content/registration.

**NOT RUN:** Linux CMake configure, GCC/Clang compile, any Vulkan
or GLES3 CTest execution, pixel capture, CI workflow completion,
on-device GPU screenshots/performance, Android-specific GLES and
the prolonged real four-owner erosion/fall simulation.

Vulkan `tests/render/vulkan_tests.cpp` historically regards no driver
as a non-failing skip. **The new forest test intentionally does not**.
The test's actual behavior and portability are unknown until
real Linux compile/runtime evidence. A requested hardware result
cannot be inferred from uploaded source.

## Exact Linux smoke commands

On Ubuntu 24.04 with Mesa Vulkan and EGL/GLES3 packages and Xvfb:

```bash
sudo apt-get update -q
sudo apt-get install -y build-essential cmake ninja-build \
  libx11-dev libegl-dev libgles-dev libvulkan-dev \
  mesa-vulkan-drivers libgl1-mesa-dev xvfb xauth

git fetch origin main integration/terraweave-physis-stage70-20261120
git worktree add --detach ../AIVERSE-terraweave-linux-gpu \
  origin/integration/terraweave-physis-stage70-20261120
cd ../AIVERSE-terraweave-linux-gpu
git status --short
AIVERSE_TERRA_LINUX_REQUIRED_SHA="$(git rev-parse HEAD)" \
AIVERSE_TERRA_LINUX_JOBS=4 \
  bash tools/terraweave-linux-gpu-acceptance.sh
```

Alternatively inspect the
`.github/workflows/terraweave-linux-gpu.yml` Actions run.
Require explicit completed success with saved `provenance.txt`,
`vulkan.log`, `gles3.log`, JUnit XML and actual PPM files.
Do not call a workflow that is queued or cancelled a PASS.

## Limitations and next stage

- Mesa software renderer under Linux is meaningful GPU API,
  GLSL and RHI-parity evidence, but not a substitute for a
  mobile hardware GPU or Android EGL GLES3 validation.
- Image presence and changed-pixel thresholds are objectively
  verifiable; actual forest quality, alpha halo, shadow accuracy,
  temporal LOD switching and performance still require visual
  human review and hardware measurements.
- Any API/include/link errors discovered by CI must be fixed in
  their owning test or integration code without suppressing a real
  Vulkan/GLES backend or relaxing the proof requirements.
- Long PHYSIS × NEREID × TerraWeave × Box3D erosion and plant-fall
  remains a separate integration test; no promise of completion
  is implied by this stage.

**Next concrete action:** inspect the first actual CI/Linux compiler
diagnostic, fix if needed, and record its tested SHA. Then implement
the sustained authentic four-owner world test. Follow the mandatory
per-stage independently committed handoff protocol; PR #53 remains
Draft until integration acceptance is reviewed.
