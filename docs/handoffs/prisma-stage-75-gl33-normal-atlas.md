# PRISMA Stage 75 — GL33 four-layer terrain normal atlas (source implementation)

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous handoff: docs/handoffs/prisma-stage-74-metal-high-detail-validation.md
Do not merge main or base.

## Why
User Apple M3 Ultra results:
- consolidated CTest 115/115 PASS (earlier revision).
- GL33 HDR: 24 shader variants and 54/54 HDR pixel probes PASS with
  fallback tier=1 detail=0, because preferred terrain high+detail used
  18 active fragment texture units against GL limit 16.
- Metal HDR after Stage-73 sampler sharing: 24 variants PASS,
  54/54 HDR pixel probes PASS and no preferred shader error/fallback.
- Metal stationary TAA: x drift 0.1678, y 0.2029, edge transition 2 PASS
  (earlier shader revision).

GL active *texture units* differ from the Metal sampler-state limit.
Metal sampler aliases alone cannot reduce distinct GL sampled textures.

## Source changes
1. `f8dce4c7977f4ed9f0d746c386ad2ffe5b0f9d43`:
   `modules/rts/view/RtsTerrainRender.h` tracks
   `layerNormalAtlas_`, `layerNormalAtlasSize_` and counts atlas
   texture in existing GPU known-memory accounting.
2. `c245ddd15477c4b78aaa11594edde79206e98c42` and
   `967e7bf4df62833310a466cfa09b6011adfdadd8`:
   `modules/rts/view/RtsTerrainRender.cpp`:
   for GL33 only, read source normal maps with the same content path and
   image decoder used by TextureCache. Build one RGBA8 2x2 atlas with
   independent repeat-wrapped padded tiles (16-pixel gutters), optional
   generated mips, max source tile dimension 2048, fall back if loading
   or allocation fails. Missing layer normals get flat-normal pixels.
   Bind atlas to texture slot 9 (instead of four normals in slots 9..12).
   Set U0 blend.w to content-tile normalized scale.
   Emit `TERRAIN_GL_NORMAL_ATLAS=1` only for a valid GL33 atlas.
   Destroy atlas at shutdown; Metal and other RHI paths untouched.
3. `270d7bbafb7287e9be2a14728bfbd40c2bfc348b`:
   `modules/rts/view/RtsTerrainShaders.h`:
   under the new GLSL-only macro, sample all four authored normal layers
   from tex9 quadrants, with per-layer UV tile scale and explicit gradients
   to avoid derivative jumps on wrapped UV. Keep individual UV scales,
   blending weight and normal mapping logic. MSL keeps its Stage-73
   sampler-sharing implementation.
4. `308d19dbfe4f8a7478f76b205f4790e7d493ba4c`:
   `tests/render/HdrShaders.h`:
   before GL33 quality fallback, try full tier=2, detail=1 with the
   GL normal-atlas shader macro and explicitly print accepted variant.
   If it fails, retain the safe bounded non-detail tier fallback.

## Design/limitations
- The intended shader resource change replaces four independently
  active normal texture uniforms with tex9, reducing the theoretical
  active GL fragment unit count by three (18 -> ~15).
- The existing RHI has 2D textures but no Texture2DArray in TextureDesc;
  this stage does not add a cross-RHI API or change texture binding ABI.
- GPU texture content is a 2x2 atlas; source textures of unequal dimensions
  are resampled to the largest source width/height via a nearest CPU pass,
  so exact full-resolution material pixel parity is not guaranteed.
- Padded mipmaps and textureGrad reduce wrap seam artifacts; very distant
  mips still need visual inspection and potential UV/mip confinement.
- Allocation failure / oversized input / unreadable source retains the
  existing production tier fallback. Do not assume full-detail works on
  all GL33 devices.
- This is a tier/source variant only for GL33; Metal preferred High+Detail
  shader stays untouched. Actual Vulkan/GLES work remains separate.
- The standalone HDR fixture validates a shader variant's compile/link,
  not actual uploaded normal atlas pixels. A full terrain material scene
  is needed to verify 4 distinct normal maps, UVs, and seams.

## Checks actually performed
- GitHub write acknowledgments and source readback of all 4 modified files.
- Source structural checks: atlas GPU accounting, allocation/cleanup,
  GL33 device gate, shader define, binding slot 9, U0 atlas scale,
  four explicit shader normal-layer selectors and textureGrad,
  optional fixture full-detail attempt and fallback.
- NOT RUN by agent: native CMake/clang, GLSL shader compiler/linker,
  Apple GPU tests, full CTest, PPM capture, material pixel parity.
- 115/115 CTest and Metal/GL HDR baselines listed above were run by user
  *before this stage*, not evidence of correctness of this code.

## Next action: Mac compile, then GL33 shader + HDR probe
```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
cmake --build build/prisma-final --parallel 4
set -o pipefail
mkdir -p build/prisma-final/prisma-gpu
AIVERSE_HDR_VERIFY=1 \
 AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/gl33-stage75.ppm" \
 build/prisma-final/aiverse_hdr_gl \
 2>&1 | tee build/prisma-final/prisma-gpu/gl33-stage75.log
```
Require `terrain high (LDR/HDR): GL33 normal atlas shader PASS, tier=2
detail=1`, 24 fixture variants PASS and all 54 HDR probes PASS.
Then test a REAL terrain scene with four authored normal maps to check
the atlas upload and material pixel parity; standalone HDR probes alone
do not validate those resources. Record exit code and file existence.
If any failure, fix it before considering this stage accepted.

After that: GPU multi-view history/fence lifecycle, Vulkan/GLES builds,
and one final consolidated CTest when functionality closes.
Every additional bounded fix requires separate docs/agent handoff.
