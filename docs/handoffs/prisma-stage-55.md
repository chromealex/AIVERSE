# PRISMA Stage 55 — concurrent 4-texture Shader Graph and HDR Auto Exposure

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-54.md
Pre-handoff branch HEAD: 7fca503b79ed63f619f41b4e53461fd72186019f
Main untouched. No merge without explicit approval.
User sequencing: finish ALL functional features first; then native build, CTest,
GPU backend image parity/profiling and fixes as a separate verification phase.

## Functional issue addressed

The old postfx final composite reserved texture slots 0..4 for scene,
Bloom/Tilt/Shafts and LUT, graph texture slots 5..8 for up to FOUR authored
Shader Graph textures, and always reserved slot 8 for HDR Auto Exposure.
The fourth graph texture therefore conflicted with the exposure sampler.
`Prisma::resolve` silently skipped Auto Exposure when a graph had four
textures even on capable HDR/High backends. This stage removes the fixed
sampler collision, preserving the runtime's existing non-exposure
composite fallback if the expanded pipeline fails on a real backend.

## Committed changes (exact SHAs and paths)

1. `72ac594f5208f6909d33bd99924a3f002e8a1e80` —
   `engine/render/PrismaCompositeTextureLayout.h` **new** GPU-free
   constexpr layout. Fixed composite: slots 0..4. Shader Graph 0..4
   project textures: slots 5..8. Auto exposure: slot 8 if the graph
   has 0..3 textures; slot 9 if all four graph bindings exist.
   `plan(graphCount, exposureEnabled)` supplies `valid`,
   `exposureSlot`, `textureSlots`; five compile-time
   `static_assert` guards cover common layouts/overflow.
2. `ce1cfedad1e30dc6007c12e66c41493f694214c9` —
   `engine/render/PrismaShaders.h`: conditional
   `PF_AUTO_EXPOSURE_SLOT9` chooses `TEX9` for HDR exposure;
   legacy `TEX8` unchanged for 0..3 Shader Graph textures.
   Exposure still runs before ACES/sRGB in linear HDR.
3. `b8d658883159210e0b1b2047f8a47ab3d3138e27` —
   `engine/render/Prisma.cpp`: GLSL/MSL program builder now
   declares `TEX0..TEX9` when requested. `composite()`
   computes the shared texture plan after compiling the graph,
   uses it for bitmask/declarations/`ShaderDesc.textureSlots` and
   adds the slot9 preprocessor flag only when necessary. `resolve()`
   binds the exposure texture using the same CPU-only plan. Invalid
   graph size is rejected, not silently aliased. Existing fallback
   from exposure variant to same graph without exposure, then plain
   base composite, remains.
4. `ee50124f545fc55a3bf12f70b9b473fdd4395b39` —
   `engine/render/Prisma.h`: `ViewEffectReport` now records
   `shaderGraphTextures` and actual final
   `autoExposureTextureSlot` (default -1 when inactive).
5. `ccaf0e554273bfb7b0342e59f5f727779ffcfe3b` —
   `Prisma.cpp`: reset the per-view binding report on each resolve;
   only report a real exposure sampler after final fallback
   selection. Update previously stale Stage 53 skip reason.
6. `e183ee76a974252451cf49b867e4617904e614d9` —
   `apps/editor/windows/PrismaWindow.cpp`: show captured graph
   texture count and actual exposure sampler slot per stable view
   ID, without creating/compiling resources during UI inspection.
7. `a799ef8dd993d2839de1a58e7806cc30bb644212` —
   `docs/prisma.md`: Stage 55 runtime/format/RHI/verification contract.
8. `7fca503b79ed63f619f41b4e53461fd72186019f` —
   `skills/visuals/aiverse-prisma/SKILL.md`: shared slot policy,
   fallback, diagnostics and handoff guidance.

This file is a separate handoff-only commit on the same branch, after
the code and skill/design documentation. Fetch latest PR #26 HEAD
before starting the next stage.

## API and shader binding invariants

```cpp
const auto layout = PrismaCompositeTextureLayout::plan(graphCount, autoExposure);
if (!layout.valid) { /* reject invalid graph */ }
// graph TEXTURE_i binds Device slot 5+i (maximum i=3)
// exposure binds Device slot layout.exposureSlot (8 or 9)
// ShaderDesc.textureSlots == layout.textureSlots (9 or 10 when exposure)
// PF_AUTO_EXPOSURE_SLOT9 is emitted only for slot9.
```

The same layout must be used for all four owners: C++ shader declaration
texMask, generated GLSL/MSL exposure branch, `ShaderDesc.textureSlots`
and runtime `Device::setTexture`. Cache key `effectMask + ShaderGraph
JSON` is sufficient because texture count follows graph JSON.
No new JSON or MCP schema is introduced; all effects keep their
existing opt-in gates. Auto Exposure remains disabled unless
authored, HDR/High and `AIVERSE_PRISMA_AUTO_EXPOSURE=1` are set.

When the expanded 10-slot shader fails to compile/link or the
backend cannot bind it, the code retains the old two-stage
fallback. The Stage 53 effect report marks `encoded=false` for
Auto Exposure when the final mask omitted it and logs a
representative reason. The editor shows actual last-encoded
slot, not a guessed capability, and does not trigger GPU work.

## Evidence and exact verification status

Performed:
- Inspected latest connected GitHub branch and Stage 54 handoff
  before editing. Updated only `feature/prisma-postfx`.
- Inspected the actual existing shader builder, final composite,
  graph compiler, GL sampler binding, Metal sampler array and
  Vulkan descriptor slot constants: GL's `GfxGL.cpp` loops
  through `ShaderDesc.textureSlots` to assign sampler locations;
  Metal `GfxMetal.mm` has slot index 9 within its binding arrays;
  Vulkan `VkShaderCompiler.h` declares 32 available texture
  descriptor positions; ShaderGraph has an existing four
  independent image limit.
- Read back new layout header, Prisma shaders, Prisma runtime,
  Prisma header and Prisma editor from GitHub after all code
  changes. **25/25 static source presence/ordering/ownership
  checks passed**, including bitmask, macro, high slot,
  fallback, runtime device binding and per-view report.
- Header includes five C++ compile-time `static_assert` checks;
  these are in source but have **not** been compiled natively.

**NOT RUN:** native CMake configure/build, clang C++ compile/link,
GLSL/MSL/SPIR-V shader compiler, CTest, `./run-tests.sh`, editor/
client execution, GLES/WebGL/Metal/Vulkan pixel capture, GPU sampler
limits/capability probing on real hardware, CI, GPU fence tests
or frame-time measurements. No native or visual PASS is claimed.

## Known limitations and next bounded functionality

- Slot 9 is supported by the inspected source-level bindings,
  but compatibility with every GLES/WebGL/mobile/driver variant
  needs native shader builds and real GPU execution.
- As before, effect `encoded` is not actual GPU completion and
  `pipelinePrepared` is not proof of visual correctness.
- Existing classic postfx DoF uses normalized hardware depth,
  not physically intuitive lens distance in world units; a robust
  genre-independent DoF needs linear camera depth, near/far
  handling and foreground/background occlusion guards.
- The broader production PRISMA functional catalog still needs
  a separate audit of DoF, exposure metering, color space,
  motion blur, reactive masks, transparent edges, volumetric
  composition, temporal upscaling and editor usability.

Suggested Stage 56: implement an opt-in **world-distance / metric DoF
focus mode** with explicit perspective/orthographic projection
semantics while preserving the old hardware-depth mode and serialized
`postfx` schema. Consider alpha-preserving defocus and
silhouette-aware sample rejection, with read-only editor metadata
and a new independently committed Stage 56 handoff. Finish
functional gaps before any native test phase. Never claim vendor
TSR/FSR/DLSS without implementing it.

## Mac checkout with remote pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If an existing worktree is rebasing/conflicted, leave it untouched
and create a separate clean worktree based on
`origin/feature/prisma-postfx`. Never reset/force push changes
owned by concurrent agents. Keep PR #26 Draft; no merge to main
until the user explicitly authorizes it.
