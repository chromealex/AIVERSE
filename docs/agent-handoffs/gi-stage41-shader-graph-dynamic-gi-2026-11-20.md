# AIVERSE GI — Stage 41: textured Shader Graph + dynamic GI coexistence (2026-11-20)

## Branch, exact commits, and test status

- Branch: `feature/gi-lighting-portable-backends-20261119`, draft PR #47 targets the original GI branch, not current main.
- `535db629c1cc0cc6941a2614102a83f8bde0ea0b` — new `engine/render/GiShaderGraphTextureSlots.h`, capability-gated dynamic GI / Shader Graph slot allocation helper.
- `aae310f71ffc463c75ece99fa3d182ff443f477a` — `Renderer3DModels.cpp`: opt-in coexistence when fragment texture slots permit it; pipeline shader graph generated GLSL/MSL sampler indices begin after final GI slot rather than aliasing tex13. Full Shader Graph still works alone on constrained devices.
- `e7916efda106916851f6970c42e5922f18fe0b32` — `Renderer3DModels.cpp`: draw submission uses the SAME texture base as shader generation, rebinding graph textures on pipeline changes even if material identity is stable, because GI/non-GI variants may move graph texture bindings.
- `d7a9b6e0aaca9933db0e6f2028aaba4bde709dbb` — `tests/render/gi_texture_budget.cpp`: pure source contract for GLES 16-slot, Vulkan/Metal larger descriptor budgets, invalid GI permutation rejection and compatibility with graph-only legacy layout.
- **SOURCE ONLY. NATIVE C++ BUILD, GLSL/Metal SHADER COMPILE, CTEST, GLES3 WEBGL2, VULKAN GPU, METAL CAPTURE AND VISUAL PIXEL PARITY ARE NOT RUN.** Do not assert actual shader runtime readiness.

## API / contract

The renderer uses `GiShaderGraphTextureSlots.h` to compute the first free texture slot for Shader Graph: no GI -> legacy slot 13; single dynamic GI -> graph starts at 14; fine R8 validity -> 15; fine+far dual cascade -> 16; Metal-only fine+medium+distant -> 17. The dynamic GI atlas keeps tex13 and never overlaps a texture used by the selected graph. Shader source permutation and `drawModelBatch` use the exact same helper, and the draw path rebinds graphs on a changed pipeline even for the same Model/Material GPU state.

`dynamicGiEligible` now permits a textured graph **only if** the required slots fit in `gfx::Device::maxFragmentTextureSlots()` for the active device. If they cannot fit, dynamic GI is excluded from this draw and the existing graph is shaded without it — no silent stealing or truncated binding. The graph material's old non-GI pipeline remains compatible. This respects GLES3's 16 fragment texture minimum: one dynamic atlas + up to two graph textures may fit; validity + one; dual cascade + any textured graph cannot fit on a 16-slot device. Metal 31 and capable Vulkan higher descriptor caps may support more. Graph texture slots >=16 on Metal use the existing constexpr sampler fallback (no extra [[sampler(16)]]).

This is limited to PBR model draws and current `Renderer3DModels.cpp` machinery. It does not magically provide GI to terrain/custom render passes or all vegetation shader families. It also does not change GPU light generation or baked lightmap formats. Low-quality/unlit, unsupported GPU-instance/material-palette and disabled HDR compute variants retain old eligibility gates.

## Known risks and next precise actions

- Material Shader Graph may generate arbitrary vertex/fragment texture expressions. Test actual GLES3 and Vulkan/Metal shader compilation and binding, especially graph vertex tex fetch vs fragment stage limits, skinned/morphed variants, alpha-tested foliage and per-material overrides. C++ source test alone cannot prove resource compatibility.
- Check GPU and CPU pixel fixtures with a textured PBR model under no GI, single GI, fine-validity and dual cascade. Verify no fallback to black textures or swapped sRGB/linear graph samples when switching GI on/off without material changes.
- Draw batching with changing graph/GI variants must preserve ordering and material-state grouping; review MDI/ICB guard paths and ensure correct selected shaders.
- Then implement a separate common custom-terrain/foliage receiving-shader GI binding API (its existing texture slots may differ), and run native compiler/CTest/full acceptance once all features are finished. Reconcile with current main and parallel render branches before merging.
