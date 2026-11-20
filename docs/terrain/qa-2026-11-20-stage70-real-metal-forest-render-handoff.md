# TerraWeave Stage70 — real Metal forest renderer QA fixture

Date: 2026-11-20
Repo: `chromealex/AIVERSE`
Working branch: `integration/terraweave-physis-stage70-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Why this stage

The previous real Metal acceptance was already green at
`b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`: 4/4 SpectraFX
GPU tests passed with no skips and the seven-frame PRISMA/VFX
review passed all five pixel predicates. It does **not** verify the
actual TerraWeave Arbor/Flora forest render path, tree materials or
impostor atlas on real hardware. The large 271/271 native CTest
baseline remains tied to `b17e775a922d9dc50981011c8f6e25d85fc7adec`.
Mac Release Stage114 retirement tests 2/2 PASS were separately
reported, but the terminal excerpt did not include a tested SHA.

## Code and exact committed source

1. `510d1390210ff1e2063859c2c309914313a331ea`:
   create `tests/terraweave_metal_forest.mm`, a real Apple GPU fixture
   using production `gfx::createMetalDevice`, `Renderer3D`,
   `ArborForestAdapter::addPrototype/uploadModels/submit/releaseModels`,
   offscreen RGBA8/depth render targets and `readColorRGBA8`.
2. `d7439549a1ca6a3807587437992c41474c582a23`:
   move the impostor UV/azimuth sampling **inside** the actual GPU
   render frame, using the same `Renderer3D::cameraPos()` that
   drives the two captured camera views. No out-of-frame
   `beginScene` calls.
3. `f872f5f2bb83c49a7b37013fef2a87fbcd6e0e3a`:
   register CMake target `aiverse_terraweave_metal_forest_test`
   and CTest `terraweave_metal_forest` on macOS when tests enabled.
   Links existing production Runtime, Models, World, Zstd, Render2D;
   no new rendering engine or alternate backend was created.
4. `03cefb9f5609f09d0e32c9b0b8de8aafec21b69a`:
   add `tools/terraweave-forest-metal-acceptance.sh`, strict,
   clean-source pinned Mac runner. Configures the existing
   `build/terraweave-main-acceptance` directory, builds exactly the
   new target, checks CTest registration and runs just that test.
   GPU absence is **FAIL**, not a CTest SKIP.
5. `47e9d4a11852cb656d3d329750911afe1c86dfca`:
   test the *actual* `buildFloraBatches` distance policy
   selects High near and Impostor at 240m before the two
   representations are compared at an equal screen distance.
   This separates distance-switch correctness from GPU pixel
   output at a readable capture size.

Changed tracked files are only:
`tests/terraweave_metal_forest.mm`,
`CMakeLists.txt`,
`tools/terraweave-forest-metal-acceptance.sh`.

## Behavior tested by new fixture

- Shared branch-based tree geometry is uploaded once per species,
  not as one GPU model per plant.
- Full-tree impostor has eight azimuth variants and **two alpha-mask
  material layers**: bark and foliage. Actual CPU atlas contains both
  transparent/opaque texels; no green opaque quad substitute.
- Real Renderer3D submits high-detail branches and near-equivalent
  far impostor into independently allocated GPU RGBA8/depth targets.
  Baseline vs branch and baseline vs impostor must each affect
  nontrivial pixel counts. The impostor's drawn bounding rectangle
  must also contain holes relative to the empty scene; this is
  a conservative alpha-cutout GPU coverage check.
- Render with a second camera yaw; the same CPU material view
  selector must use a different azimuth UV offset.
- Render a separate 192x128 viewport in addition to 256x256 and
  verify visible geometry survives the new size.
- Save six **real Metal P6** frames when
  `AIVERSE_TERRAWEAVE_FOREST_CAPTURE` is set: baseline,
  branch-lod, impostor-a, impostor-b, baseline-resized,
  branch-resized. No synthetic replacement allowed.
- Wait for `gfx::Device::waitForResourceIdle()` **after all GPU
  submissions and color readbacks**, before releasing models,
  Renderer3D pipelines and the offscreen textures.
  This asserts an explicit GPU-safe retirement boundary; it
  **does not** yet stress repeated editor window create/destroy
  or deferred retirement under concurrent in-flight resizes.

## Mac Studio run (NOT YET EXECUTED)

First inspect checkout status; do not reset other agents' work:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
AIVERSE_FOREST_REQUIRED_SHA="$(git rev-parse HEAD)" \
AIVERSE_FOREST_JOBS=4 \
  bash tools/terraweave-forest-metal-acceptance.sh
```

Evidence directory:
`build/terraweave-main-acceptance/forest-metal-qa/<HEAD-12>/`.
The script fails on missing/empty required PPM frames, a missing
CTest registration, a dirty checkout, the wrong SHA, failed compile,
GPU image readback or any failed pixel/LOD/retirement assertion.
Save configure/build/CTest logs, JUnit, provenance, six PPMs and
the final PASS/FAIL text. Validate the actual frame appearance,
halos and material cutouts visually before claiming shipping
foliage quality.

## Verification actually done

**DONE:** connected GitHub read of the authoritative ForestAdapter,
ModelAdapter, ImpostorAtlas, Flora and GPU renderer API; existing
production `spectrafx_metal_scene.mm` readback/capture pattern;
source code commits above; GitHub readback of the new test, CMake
registration and runner. Compare after these source commits, from
prior documented integration head `d8c08e31df97731888fd06bfca5fcacc625bd611`,
showed exactly 3 changed files, 5 source commits and no
unrelated modifications.

**NOT RUN:** Objective-C++ AppleClang compilation of the new test,
CMake configure after new target, GPU execute, any screenshots,
actual resize/close/open editor stress, Android GLES, Linux Vulkan,
long four-owner PHYSIS/NEREID/TerraWeave/Box3D erosion runtime.
A screenshot capture can validate visual output only when it was
actually obtained from a GPU; source authoring alone is no evidence.

## Remaining risk and next stage

The new GPU fixture intentionally uses manually selected High and
Impostor render batches at the same pixel scale after separately
validating the real Flora distance LOD policy; it is not a substitute
for scrolling a full streaming forest across distance thresholds
in the editor. A polygonal leaf card near material is still an
opaque fallback until a project supplies cutout assets; the
far impostor is alpha-masked. A true editor viewport lifetime
test and forest streaming performance are separate remaining gates.

Next agent: **run the single new Metal forest CTest on Mac Studio**,
record the first actual compiler/pixel failure and repair it without
disabling GPU tests or relaxing thresholds without evidence. Then
validate Vulkan/GLES on supported target hosts and long authentic
four-owner erosion/fall. Preserve the prior hardware acceptance
results and record any future stage in a separate committed handoff.
PR #53 remains Draft.
