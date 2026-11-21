# AIVERSE lighting integration — Stage 6: GLES/mobile direct fallback and spot-aware GI source bridge (2026-11-20)

## Branch and exact code commits

- Working branch `integration/lighting-main-stage1-20261120`; originally based on current `main` `f9aed9a705982e2c6b77b29869e309995ce8b163`. Do NOT merge until native/GPU acceptance.
- Source commit `5e7db23df95e80ec05ba29ffd54810daa796fc55` imports/adapts `engine/render/LumaWeaveLegacyBridge.h`, `LumaWeaveMobileFallback.h`, adds `tests/render/lighting_luma_mobile_fallback.cpp`.
- Test scoring fixture correction `15f7813a3460d289b29b1530fea706088693b2e7` — red source intensity raised from 8 to 16 so its score actually exceeds the competing green source (linear luminance weights 0.2126/0.7152), matching the test's deterministic rank assertion.
- CMake CTest registration `9706a64f11f2400b350e62d2ec5e6308f135ab8d`: `lighting_lighting_luma_mobile_fallback`. **12 lighting regression test sources** now registered through Stage6.

## Implemented, usable opt-in path

- `LumaWeaveMobileFallback::plan(registry, focus, viewRadius, maxLegacyLights)` converts the bounded high-key native light source snapshot into the existing current-main genre-independent `PointLight` descriptors. Chooses only visible and positive-energy point/spot lights, stable importance and key ordering; returns explicit `dropped`, `lossy` and `retainedImportanceFraction` diagnostics rather than silently pretending an old mobile raster map supports all 4096 lights. The current main `LightSystem::kMaxLights=1024` is honored.
- `LumaWeaveMobileFallback::enqueue` is intentionally fail-closed if `legacy.count()!=0`, `staticPointLightsActive()`, quality Off or the plan exceeds the legacy map budget. This makes the scene select ONE direct-light owner and avoids double-drawing already-submitted legacy dynamic lights. Call only between the host's real `Renderer3D::beginScene` and `Renderer3D::render`. It is NOT automatically invoked in the game/editor; an explicit caller must choose the route.
- `lumaFromLegacy(PointLight,key,contributesToGi)` converts existing authored native point/spot lights without inventing stable identities.
- New `lumaGiMovingSources()` maps only `contributesToGi` point AND spot lights into the current `GiMovingLight` CPU reference. Earlier donor `lumaGiPointSources` erroneously rejected indirect spot sources despite the current GI CPU reference having explicit normalized cone direction and inner/outer cosines. The legacy named function remains as an alias forwarding to the cone-aware method, preserving source compatibility. No static-baked direct source is added to dynamic GI by default.
- CPU fixture checks stable sources, 1024 map cap admission via plan, candidate ranking/overflow diagnostics, spot conversion in direct raster and GI source, legacy authoring compatibility and invalid arguments.

## Platform and ownership

- This is a usable **host-callable** mobile/GL/GLES direct fallback through the existing LightSystem. It does not write a new GLES shader or auto-enable Luma in normal renderer/editor; existing main LightSystem already owns its raster path.
- This stage leaves current main rendering, PRISMA, SpectraFX, PHYSIS, TerraWeave, GLES RHI, Vulkan and Metal untouched.
- CPU/GLES indirect GI still requires later material sampler integration for Stage5's RGBA32F atlas. The fallback can lose low-ranked direct lights when above its capacity; diagnostics must be surfaced rather than hidden.

## Verification truth and continuation
- PERFORMED: connector source review for both donor headers and current-main PointLight/LightSystem signatures; successful GitHub code/CMake commits, source-level review of score arithmetic and correction.
- **NOT RUN:** Clang/GCC build, the 12 registered focused CTests, GL/GLES raster screenshots, Vulkan/Metal compute/render, shader compilation, frame timings, full AIVERSE suite.
- NEXT: compile 12 focused CTests against exact branch HEAD and fix failures from real logs. Then host-wire explicit fallback after beginScene/before render, integrate current Renderer3D material sampling for CPU GI RGBA32F, bring Metal/Vulkan direct & GI compute with real RHI fences, execute visual/perf parity, only then merge to current main.
