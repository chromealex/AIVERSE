# Hybrid GI — Stage 31: Vulkan/GLES/Metal texture-slot admission (2026-11-19)

## Stage identity and exact source changes

Branch `feature/gi-lighting-portable-backends-20261119`, continuing Stage 30; draft PR #47 targets `feature/hybrid-dynamic-gi` and remains unmerged. SOURCE ONLY; no native build, shader compilation, actual GPU runtime, benchmarks or CTest have run.

Code commits:
- `b3e2bcedd9dfeade0b8bdb9cb8fe502edbdba865` — `engine/gfx/Gfx.h` portable `maxFragmentTextureSlots()` API (default 16).
- `51309f4871f65d89b5071a2f1ec9961a06a421f7` — `engine/gfx/vulkan/VkDevice.h` returns actual `textureSlots_` (descriptor cap).
- `589f899c6c7609375e946f319607a9eaabf1d138`, `e4ced6fefb014ebd17b1e5a3ff2e1782a8d660d8`, `b763878231a060fa89076b3605ecbe75d25ba70b` — `engine/gfx/gl/GfxGL.cpp`, queries GLES3/GL fragment `GL_MAX_TEXTURE_IMAGE_UNITS`, records it, exposes it via RHI.
- `db6b4109ff8d1c95c3a1c448742d9a3984bca974` — `engine/gfx/metal/GfxMetal.mm` exposes Metal texture binding capacity 31 (not to be confused with hardware sampler count 16).
- `830164db102f6cf2ee9476f0761ae935520e7403` — `engine/render/GiTextureBudget.h`: pure constexpr slot policy.
- `639b1e46241c691160a7e447fc37ec697301eaf8` — `engine/render/Renderer3DModels.cpp`: applies slot policy at primary/secondary/third GI overlay binding and draw shader eligibility.
- `21acd0131630e54c7b055fe55638c509813141f8` — `tests/render/gi_texture_budget.cpp`, Release-safe source test.
- `3e356cce009338c40e9867e4d8951c3deeee64f0` — `CMakeLists.txt` registers `gi_texture_budget` with CTest.

## Contract

The common model shader uses fragment textures `tex0..12` in baseline view. Dynamic GI needs `tex13` for one L1 atlas (14 slots total); with per-probe validity `tex14` (15 slots); dual-cascade `tex15` (16 slots); Metal-only third cascade `tex16` (17 slots). Slots are **texture binding count**, not Metal filtering samplers. `giFragmentTextureBudgetAllows` rejects impossible combinations (far without validity, third without far) and devices with insufficient max fragment texture units.

- GLES 3.0 baseline minimum supports 16 fragment units: only up to dual cascade can fit and CPU-probe fallback remains available; no GLES compute path is required.
- Vulkan RHI descriptor count is queried from the selected physical device; under 14 slots the opt-in PBR GI overlay is rejected, rather than silently compiling a shader whose descriptor indices cannot exist.
- Metal reports 31 texture slots and can retain its existing third-cascade optional binding. Metal fragment `sampler` limits remain independent; GI texture reads use `.read()` without extra sampling states.
- The existing Shader Graph overlap at `tex13+` remains excluded. No runtime GI enable-by-default, ECS schema, material defaults or baked indirect ownership changed.

## Actual verification and limitations

**Performed:** GitHub reads/commits and CTest source registration only. **NOT RUN:** compiler, CTest, GL/GLES shader link/texture reads, Vulkan SPIR-V/driver descriptor validation, Metal resource limits test, Android/WebGL2, mobile memory/perf. Results are not a validation PASS.

Potential limitations: Real mobile hardware can expose tighter total-varying, uniform, extension and HDR framebuffer budgets than this texture-slot count; `maxFragmentTextureSlots` does not independently guarantee all PBR material shader variants can compile. For full parity, run shader-variant permutations and pixel fixture on real targets, and improve/disable GI quality dynamically on device-specific errors. A failed direct overlay setter may leave a previous valid pointer unless its caller unbinds, so hosts should use the Stage 29 guarded `GiDynamicRuntime::bind` and clear before owner destruction.

## Next precise action

1. Build and run: `cmake --build build/gi-vk --target aiverse_gi_texture_budget_test aiverse_gi_gpu_small_scene_test aiverse_gi_gpu_ticket_queue_test -j8`; `ctest --test-dir build/gi-vk --output-on-failure -R '^gi_(texture_budget|gpu_small_scene|gpu_ticket_queue)$'`.
2. Test GL ES3 core 16-unit hardware with dynamic GI single/dual shader and verify RGBA32F nearest texel fetch, HDR render target and shader graph exclusion. Compare with Vulkan actual sample descriptor count and Metal 17-texture MSL.
3. Implement a runtime quality/backpressure policy for GLES CPU GI (maximum trace rays/probes per frame, graceful off/low fallback) rather than running the expensive reference tracer at arbitrary world size. Add explicit tests and shader compile matrix, then stage-specific handoff.
4. Reconcile current main texture-RHI/backend changes and run the full acceptance only after remaining GI/light functionality is done.
