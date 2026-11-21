# PRISMA Stage 75 hotfix — recover GL33 atlas member declaration

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-75-gl33-normal-atlas.md
Main and base untouched.

## Actual user-reported native Mac build error
All-target macOS C++ build fails around 61% in translation units including
modules/rts/view/RtsTerrainRender.h, including RtsShapes.cpp, RtsView.cpp,
RtsTerrain.cpp and RtsLook.cpp:

```
RtsTerrainRender.h:111:47: error: use of undeclared identifier 'layerNormalAtlas_'
                detailNrmTex_,detailSplatTex_,layerNormalAtlas_})
```

Build returned make Error 2. The separate GL33 HDR invocation followed a
failed build and printed the old fallback tier=1 detail=0 with 54 GPU probes
PASS. That does NOT validate newly added Stage-75 atlas source.

## Root cause
New C++ class fields were accidentally inserted with literal backslash-n
sequences inside a // comment in RtsTerrainRender.h, so the compiler did
not see a declaration for layerNormalAtlas_ and layerNormalAtlasSize_.
The GPU accounting method used the undeclared member at line 111.

## Source fix
Commit `8c4fd1daf4643f3625a6db0f88cd870198e349ba`:
`modules/rts/view/RtsTerrainRender.h`: turn escaped literal \n tokens
into physical newlines; make the two data members valid class fields:
```cpp
// GL33: four repeatable layer normals packed into one atlas sampler.
gfx::TextureHandle layerNormalAtlas_;
int layerNormalAtlasSize_ = 0;
bool texturesLoaded_ = false;
```
No shader policy, texture upload, destruction or RHI change.

## Verification actually done
- GitHub readback after edit: atlas field separately declared on line 188,
  atlas size on line 189, texturesLoaded_ on line 190.
- No bad literal backslash-n remains in that declaration.
- Checked RtsTerrainRender.cpp still uses a matching atlas member.
- Native compile/CTest/GL33 fixture after fix: NOT RUN by assistant.
  All previous user build errors are actual results from before the fix.

## Next
User can incrementally recompile existing build cache and run GL33 fixture:
```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
cmake --build build/prisma-final --parallel 4
set -o pipefail
AIVERSE_HDR_VERIFY=1 \
AIVERSE_HDR_CAPTURE="$PWD/build/prisma-final/prisma-gpu/gl33-stage75.ppm" \
build/prisma-final/aiverse_hdr_gl \
2>&1 | tee build/prisma-final/prisma-gpu/gl33-stage75.log
```
Only run GPU fixture once CMake build succeeds. Inspect diagnostics and
capture; new GL33 atlas variant must actually link for both LDR and HDR,
and 54 GPU probes must pass. Standalone fixture only validates atlas
shader compilation, not authored multi-normal atlas pixel parity.

If additional compile errors occur, commit bounded fixes and handoffs.
