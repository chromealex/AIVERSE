# AIVERSE GI — Stage 36: genre-neutral point and spotlight indirect transport (2026-11-20)

## Branch, source commits and honest test status

- Branch `feature/gi-lighting-portable-backends-20261119`; draft PR #47 to `feature/hybrid-dynamic-gi`, NOT merged to main.
- **SOURCE COMMITTED ONLY. NO NATIVE BUILD, CTEST, GLSL/MSL/SPIR-V COMPILATION OR METAL/VULKAN/GLES HARDWARE TESTS WERE EXECUTED** in this stage.
- Code commits:
  - `56f5e9813465d1266dbd5d940b1f08ccaadc7a07`: `engine/render/GiDynamicProbeReference.h`, extend stable moving-light descriptor with optional spot orientation/angles, validate normalised direction, include cone in revisions and CPU indirect bounce.
  - `2e247cf0921e8e09fb53a3883a2d75034a22719f`: `engine/render/GiGpuSmallScene.h`, 64-byte GPU Lamp ABI with direction/outer cone and inner cone, validate and pack.
  - `a2b77ea5c283ce4f36545626798afe58712045ef`, `c3d40012a008a994b450aa2de66c40ec3d4f9d2e`: Metal and Vulkan BVH compute kernels evaluate cone attenuation using the light->surface vector; no fake omni bounce from spotlights.
  - `3239e9446337dc878a1281581f720bf6a3cd9a93`: `GiGpuDynamicRuntime.h`, cone changes invalidate GPU transport even when position/radius/color are unchanged.
  - `57895d453d7f82bd40cb0a73b4db6cb73a90a221`: `Lights.h`, new default-zero `PointLight::indirectMultiplier` (direct illumination unchanged, baked/static light source excluded unless expressly opted-in).
  - `7f3f33b6618bde9867d7f6a6330bc5306122d032`: `GiLightBridge.h`, stable source namespace, 32-bit key->64-bit GI key, point/spot conversion, validation, deterministic optional TopInfluence budget, explicit dropped-source statistics.
  - `15ae23aa5854b9baf1675a3c77938f1ceebafdea`: `GiLightingController::setAuthoredLights()` routes direct-authored opt-in light snapshots through Metal/Vulkan GPU or GLES CPU.
  - `eed727b1edf6ccd53d768d458a7b48fafc98c84d`, `ebe8c97265e83c05686ed338a34ceebef8feb42f`: `gi_light_bridge` regression and CTest registration.
  - `0d4c058f2f84ebfc14c18d303bca974b94d7dc7d`: `gi_gpu_small_scene` cone ABI source tests for Metal/Vulkan.
  - `87c780676f4aea84905da1548d2c81c090395326`: `gi_lighting_controller` opt-in authoring regression fixture.

## Architecture / API

- Existing renderer direct `PointLight` sources and spot cone methods remain authoritative for direct light. New `indirectMultiplier=0` is an explicit opt-in for **independent dynamic-only bounced radiance**: never automatically rebounce a static light whose energy is already baked into one indirect base. This field is appended to the public light descriptor; existing aggregate initializers keep their previous meaning and field order.
- `GiMovingLight` retains the prior five positional aggregate fields and appends `direction`, `cosInner`, `cosOuter`. `cosOuter <= -1.5` means an omnidirectional point source. Spot energy is multiplied by a bounded angle fade `clamp((dot(lightDirection, surface-lightDirection)-cosOuter)/(cosInner-cosOuter),0,1)` in CPU reference, Metal MSL, Vulkan GLSL compute. Degenerate cone widths are guarded by 1e-5.
- GPU ABI changes from a 32-byte Lamp (`posRadius,colorIntensity`) to 64 bytes (`posRadius,colorIntensity,spotDirectionOuter,spotInner`). The two compute kernels and host packing must remain EXACTLY aligned. This changes only the opt-in dynamic transport SSBO, not old saved/baked cache files.
- `GiLightBridge::collect(direct, namespaceId, maxSources, focus, overflowPolicy, stats)` requires nonzero publisher namespace and stable nonzero source keys, validates light values and normalised cones, returns sorted stable 64-bit keys. Default `Reject` refuses excess sources; explicit `TopInfluence` chooses deterministically based on focus/influence, and returns `Stats::dropped` so selection is never silently attributed to complete GI. GPU currently admits 16 sources, CPU 64. Selected source changes invalidate the affected probes in the controller.
- `GiLightingController::setAuthoredLights` is a helper for snapshots already published after ECS/world structural barriers. This is **not automatic ECS subscription**, terrain/foliage binding or persistent lighting state. Application must still call updateFrame/bind.
- Directional sun/sky/area lights continue to be handled by their existing static/ambient/direct owners; this stage adds optional indirect point and spot only. No automatic bake subtraction.

## Known limits and next steps

- Actual MSL/SPIR-V shader compiles, cross-backend spot wall color readback and GLES CPU numeric comparison are **NOT RUN**. Spot cone GPU math and ABI require native compilation and parity tests; source string tests are not proof.
- A live source with `key==0` or duplicate key cannot enable indirect GI; caller must assign stable IDs from its ECS/entity/world owner. Source namespace is mandatory for multiworld collision isolation. GPU cap 16 remains a limitation; explicit TopInfluence selection may switch near the camera as focus moves. Add hysteresis/clipmap-aware admission if visible popping occurs.
- For native ECS/render integration, publish point/spot snapshots after ECS read barriers into `setAuthoredLights()`, publish immutable changed geometry into `setGeometryRegion()`, notify `GiWorldDirtyAccumulator` and call updateFrame before binding. Do not mix shader graph textures with tex13+ yet.
- Next: Run `aiverse_gi_light_bridge_test`, `aiverse_gi_lighting_controller_test`, `aiverse_gi_gpu_small_scene_test`, `aiverse_gi_gpu_dynamic_runtime_test` after building, fix real errors, and write a tested-SHA handoff. Then test actual Metal/Vulkan spot GI pixel parity, GLES3 LDR output, main conflict reconciliation, and full acceptance in one pass when all features are complete.
