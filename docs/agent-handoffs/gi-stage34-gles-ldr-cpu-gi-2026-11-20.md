# AIVERSE GI — Stage 34: GLES3 LDR CPU probe presentation (2026-11-20)

## Branch / commits / actual status

- Branch: `feature/gi-lighting-portable-backends-20261119`, PR #47 (draft targeting original GI feature branch, not current main).
- Source commits: `980fe286566c663ad159a07f5e5afa940e276224` (`engine/render/GiPresentationPolicy.h`), `4e5eee0258c4196b120ced82ce271abb6658cc5c` (`Renderer3DModels.cpp`), `f6e12c3eb1c35b5e0aeac65029685e6e610af330` (`tests/render/gi_texture_budget.cpp`).
- **SOURCE COMMITTED, NOT EXECUTED**: native CMake/Clang/GCC, CTest, shader compiler, GPU pixel parity and Android/WebGL2 were not run.

## Problem corrected

GLES3/WebGL2 devices can support a sampled RGBA32F data texture but not an HDR RGBA16F framebuffer extension. The existing opt-in `GiDynamicRuntime` CPU tracer produced correct dynamic-only GI coefficients and uploaded them through the RHI, yet `Renderer3D::dynamicGiEligible` rejected **all** scenes lacking an HDR render target (`!hdrFrame_`). These devices silently had no dynamic GI even at `MaterialQuality::Medium` or `High`.

The generic, header-only `giDynamicColorPassAllowed(hdrScene,computeWritableProbeAtlas)` now explicitly allows **non-compute RGBA32F CPU GI textures** to be sampled by a Medium/High PBR shader into an ordinary RGBA8/LDR scene. GPU-writeable RGBA16F GI remains HDR-only pending separate LDR GPU sampling validation. This affects shader eligibility, not the actual GI math; model shader already evaluates incident bounce before its regular LDR color path. Existing Shader Graph and fragment texture budget gates still apply. No dynamic GI is enabled automatically in any game.

## Backend coverage and limitations

- GLES3/WebGL2: CPU trace → nearest RGBA32F atlas → `tex13` on PBR models; RGBA8 output when no HDR target. Requires sampled RGBA32F support, valid source geometry and explicit runtime binding. No compute shader requirement. Low material tier still excludes GI.
- Vulkan: the same sampled CPU fallback can be used with LDR when GPU compute is unavailable; the experimental Vulkan compute-written RGBA16F path remains HDR-only until tested.
- Metal: existing compute GI unchanged, with CPU RGBA32F LDR fallback possible explicitly.
- Not integrated for terrain/foliage/custom shaders; not tested on real phone. Ordinary LDR PBR tonemapping/gamma behavior still needs shader compile/pixel comparison against the HDR path.

## Next action

1. Build and run `gi_texture_budget` and real GL ES3 model shader variants in LDR/HDR; compare no-GI vs red-bounce capture and verify correct gamma/no banding.
2. Build a genre-independent host controller choosing Metal/Vulkan compute or GLES3 CPU fallback, with explicit geometry snapshot ownership, fence-aware shutdown, quality settings, stable point-light IDs and no double-counting.
3. Add tests and handoff separately; only after feature completion run full acceptance. Do not claim PASS or merge into main prematurely.
