# Main integration — GI / LumaWeave (2026-11-21)

Branch `feature/aiv20261121_main_gi` (worktree `/home/claude/wt_gi`), base
`origin/main` `2d034dd6bda65044809e9a04217587a033bc9682`. Not pushed.

## Decision summary

Main already carries the canonical lighting/GI/Luma implementation
(`lighting-main-stage1..37`): a selective re-port of
`feature/gi-lighting-portable-backends-20261119@770c7ac1d` and
`feature/lumaweave-massive-lighting@794312412` (their current heads) onto
main's own Renderer3D/RHI. The remaining unmerged GI lines are **competing
implementations** of the same runtime (own Renderer3D receiver passes, own
selective Vulkan compute, GI slots U6/U7/U8 + tex13..16 vs main's
tex13/U6/buffer7, U7 = VT). AGENTS.md: "do not restore the former parallel
GI-only backend". No head is bulk-merged (merge-tree conflict counts vs main:
hybrid 10, massive 17, portable 26, bvh-stage62 27, stage22 27, stage23 28,
reactive-stage21 29).

| Line | Head | Status |
|---|---|---|
| feature/hybrid-dynamic-gi | 9819baa6b | superseded (base of all) |
| feature/gi-lighting-portable-backends-20261119 | 770c7ac1d | absorbed/replaced by lighting-main |
| feature/lumaweave-massive-lighting | 794312412 | absorbed/replaced (NativeDriver lifetime work moot: main uses LumaHost) |
| feature/gi-bvh-precision-stage62-20261120 | 082ae5538 | **b1d08ad43 cherry-picked here**; rest = docs/Release-test fixes already in main |
| feature/gi-lumaweave-*-stage10..21 (reactive-motion-stage21) | 83032342f | unique features → step-2 port plan below |
| integration/luma-prisma-stage22-main-20261120 | e84425c16 | competing port onto old main f9aed9a70; source for step 2 |
| feature/gi-lumaweave-main-clipmap-stage23-20261120 | 46fbd0fee | competing port (child of stage22); source for step 2 |

## Step 1 — done here

- `cd385bf1a` cherry-pick of `b1d08ad43` (owner identity, original message, no
  trailers): `engine/render/GiGpuBvhValidator.h` containment tolerance is now
  one float32 ULP (`std::nextafter`) per endpoint instead of `1e-5*|coord|`
  (which reached ~1000 world units at 1e8 and admitted escaped triangles →
  false BVH pruning / light leaks). Main's file was byte-identical to the
  commit parent: no conflict. Consumers: `GiGpuSmallScene.h`,
  `LumaWeaveScreenMetal.h` (upload preflight).
- `0e99e54a1` new `tests/render/gi_gpu_bvh_validator.cpp` (AlwaysCheck,
  Release-safe, hand-built flat packets, no `GiGpuPagedBvhArena`), registered
  in the existing Lighting CPU fixture loop → CTest
  `lighting_gi_gpu_bvh_validator` (label Lighting). Covers empty/half-empty,
  near-origin 5e-6 escape, 1e8 112-unit escape, exactly-1-ULP accepted /
  2-ULP rejected at 1, 4096, 1e8, internal-node child escape at 5e7
  (4 ULP), cycle, bad leaf range, NaN.
  Note: includes `<cstdint>` first because `StaticPointShadows.h` (via
  `GiGpuBvh.h`) uses `uint32_t` without including it — pre-existing latent
  header bug, not fixed here (out of scope; fix candidate).

## What was run

- `g++ 14 -std=c++20 -O2 -DNDEBUG -Wall -Wextra -I. tests/render/gi_gpu_bvh_validator.cpp`
  → no warnings; run → `gi_gpu_bvh_validator: OK` (PASS).
- Same test against the pre-cherry-pick header (`git archive HEAD~1`) →
  FAILS at the near-origin escape check (expected; proves the test guards
  the fix).
- **NOT RUN:** CMake configure/ninja/ctest in this container — the configure
  step was refused by the session's permission policy (AGENTS.md requires
  KINGSTON build trees; `/Volumes/KINGSTON` absent here). So
  `ctest -R 'gi_|luma|lighting'` was not executed, and CMake registration of
  the new fixture is unverified by a real configure (one-line addition to an
  existing `foreach` list).
- Metal/Vulkan: nothing compiled; the header change is CPU-only C++.

## Mac / KINGSTON verification required

```
cmake -S <wt> -B /Volumes/KINGSTON/AIVERSE-builds/main-gi/release -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C ... aiverse_lighting_gi_gpu_bvh_validator_test <all aiverse_lighting_*_test> aiverse_hdr_scene
ctest --test-dir ... -R 'gi_|luma|lighting' --output-on-failure
```
Must include `lighting_gi_gpu_bvh_validator` plus the GPU-backed
`lighting_dynamic_gi_gpu`, `lighting_compute_gi_gpu`, `lighting_icb_gi_gpu`,
`lighting_icb_compute_gi_gpu`, `lighting_terrain_gi_gpu`, `studio_metal_lighting`
(large-world scenes may now reject BVH packets that previously passed —
any new rejection is a real bad packet, check the error text).

Risk: a CPU builder that produces child boxes enlarged by >1 ULP beyond the
parent (e.g. different float accumulation order) would now be rejected.
`GiGpuBvh::build` computes parent boxes as unions of child boxes, so it
should be exact; confirm with the GPU tests above.

## Step 2 — porting plan (NOT started)

Rule for every item: port onto main's existing owners (`GiMainGpuBridge`,
`DynamicGiHost`, `LumaHost`, `RenderLightingRuntime`, Vulkan TextureResources
compute, `Renderer3D::setDynamicGiOverlay` / `bindDynamicGiForCustomAt`);
never import a second Renderer3D receiver path or second Vulkan compute
backend. One numbered `lighting-main-stage38+` handoff per item. Source refs:
S21 = `feature/gi-lumaweave-reactive-motion-stage21-20261120@83032342f`,
S23 = `feature/gi-lumaweave-main-clipmap-stage23-20261120@46fbd0fee`.

1. **Large-world BVH residency** — S21/bvh-stage62: `GiGpuChunkBvhCache.h`,
   `GiGpuPagedBvhArena.h`, `GiGpuResidencyPlanner.h`, `GiGpuTicketQueue.h`,
   `GiWorldDirtyAccumulator.h`; tests `gi_gpu_chunk_bvh_cache/_lifetime`,
   `gi_gpu_bvh_sparse_upload`, `gi_gpu_ticket_queue`,
   `gi_world_dirty_accumulator`, original `gi_gpu_bvh_validation`.
   Target: feed `GiMainGpuBridge` (replace its "bounded triangle cap → CPU
   fallback" with paged residency); reuse main `GpuResourceRetirement`
   fences. Acceptance: CPU tests + Metal `lighting_compute_gi_gpu` on a scene
   above the current cap without CPU fallback.
2. **World/TerraWeave geometry feeds** — `engine/world/GiWorldGeometryBridge.h`,
   `GiTerraWeaveResidentBridge.h`, `GiGeometryScene.h`, `GiWorldPresentationSync.h`;
   tests `gi_world_geometry_bridge`, `gi_geometry_scene`, `gi_regional_geometry`.
   Target: publish into main `GiChunkedGeometry` snapshots via `DynamicGiHost`.
   Acceptance: CPU tests + native TerraWeave scene GI on Metal.
3. **GI cascades / clipmaps (far-field)** — S23: `GiClipmapVolume.h`,
   `GiClipmapValidityTexture.h`, `GiCascadeBlend.h`, `GiCascadeShaders.h`,
   `GiGpuCascadeRuntime.h`, `GiGpuClipmapRuntime.h`, `GiDynamicRuntime.h`,
   `GiGpuDynamicRuntime.h`, `GiRendererOverlayPolicy.h`; tests
   `gi_clipmap_volume/_validity`, `gi_cascade_blend`, `gi_gpu_cascades`,
   `gi_gpu_clipmap_cache/_runtime`, `gi_metal_three_cascades`,
   `gi_dual_cascade_shader`, `gi_main_renderer_contract`, `gi_main_renderer_spirv`.
   **Slot ABI must be redesigned** for main: S23 uses U6/U7/U8 + tex13..16
   and forces non-ICB; main uses tex13/U6/buffer7, U7 for VT, ICB atlas13 at
   arg26, terrain slots 6..12/21. Needs a texture-slot planner decision
   (owner: lighting agent) before code. Acceptance: GLSL+MSL compile, SPIR-V
   glslang, Metal pixel parity cascade on/off, ICB on/off, VT on/off, GLES
   fallback unchanged.
4. **Vulkan Luma direct lighting** — S21 stages 11–16: `LumaWeaveVulkanDirect.h`,
   `LumaWeaveVulkanDirectShader.h`, `LumaWeaveVulkanCommonShader.h`,
   `LumaWeaveVulkanScreenShader.h`, `LumaWeaveVulkanHdrShader.h`; tests
   `lumaweave_vulkan_direct_owner/_shader`, `lumaweave_vulkan_post_contract`.
   Target: Vulkan backend of main `LumaHost` (currently Metal-first) using
   main's Vulkan TextureResources compute (do NOT port S22's
   `VkCompute.cpp`/`VkShaderCompiler` rewrite). Acceptance: lavapipe +
   validation layer, `lighting_vulkan_compute_shader`, pixel parity vs Metal
   Luma reference scene.
5. **Vulkan Luma temporal/denoise/history** — S21 stages 17–19:
   `LumaWeaveVulkanDenoiseShader.h`, `LumaWeaveVulkanNormalHistoryShader.h`,
   `LumaWeaveVulkanHistoryPolicy.h`, `LumaWeaveTemporal.h`; tests
   `lumaweave_vulkan_history_policy`, `lumaweave_vulkan_multiframe`,
   `lumaweave_temporal`, `lumaweave_screen_history_epoch`. After item 4.
6. **PRISMA motion / reactive integration** — S21 stages 20–21, S22:
   `LumaWeaveVulkanMotionPolicy.h`, `LumaWeaveVulkanPostProcess.h`, PRISMA
   velocity hook; tests `lumaweave_vulkan_motion_policy`,
   `lumaweave_vulkan_auto_motion_bridge`, `lumaweave_vulkan_reactive_contract`,
   `lumaweave_stage22_main_prisma_contract`. Must use PRISMA-owned velocity/
   history (ownership contract) and main's RGBA16F signed-copy path if
   missing. Acceptance: PRISMA tests + Luma ghosting test on moving camera.
7. **Area lights / particle / effects bridges** — `LumaWeaveAreaLights.h`,
   `LumaWeaveParticleBridge.h`, `LumaWeaveEffectsBridge.h`, `LumaWeaveGiSync.h`,
   `LumaWeaveWorlds.h`, `LumaWeavePbrReference.h`, `LumaWeaveBackendPolicy.h`;
   tests `lumaweave_registry`, `lumaweave_backend_policy`,
   `lumaweave_gi_frame_contract`. Light sources must stay entity components
   (AGENTS entity rule; `LumaWeaveLights` registry is presentation-only).
   SpectraFX emissive particles stay SpectraFX-owned. Acceptance: CPU tests +
   Studio light-entity round trip.
8. **Stress / benchmarks** — `LumaWeaveStress.h`, renderbench scenes; only
   after 1–7; performance claims need KINGSTON Mac A/B/A.

Not to port: `LumaWeaveNativeDriver/NativeCoordinator/MetalDirect/MetalShader`
(replaced by main `LumaHost` + Luma Metal shaders), S22/S23 `Renderer3D*`,
`ModelShaders.h`, `Gfx.h`, `engine/gfx/vulkan/*` diffs, S22 `VkCompute.cpp`.
