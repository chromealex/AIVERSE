# AIVERSE GI — Stage 45: stable vegetation/HLOD collision source for GI (2026-11-20)

## Branch, exact implementation and validation status

- Working branch: `feature/gi-lighting-portable-backends-20261119`; draft PR #47 targets `feature/hybrid-dynamic-gi`, NOT the diverged current main.
- `9dc2bdafdfa28521fed39ffef054ca1ff98ef2f0` — `engine/world/GiWorldGeometryBridge.h`: explicit world-space `replaceVegetationProxy(GiGeometryScene&,chunkX,chunkZ,triangles,albedos)` using `GiGeometrySource::VegetationProxy` stable namespace and the existing incremental chunk BVH. No terrain/tree mesh regeneration, ECS mutation or new physics representation.
- `e8b28362c3be0a1e28173987540e706249bc346f` — `tests/render/gi_world_geometry_bridge.cpp`: source checks for canopy collision, independent namespace, safe LOD/removal, exact old/world collider preservation and source revisions.
- `538915a7cbc4c4b6de860a0530646190b858ccc2` — header explicitly includes `<utility>` for move of immutable authored source snapshot.
- **Actual tests:** NONE run in this connector-only environment. CTest source is existing `gi_world_geometry_bridge`, extended but NOT executed; Metal, Vulkan, GLES shader/driver, memory/performance and native C++ compilation NOT RUN.

## API / lifetime contract

A native forest, tree, impostor, grass or destructible-system owner provides an already-produced set of WORLD-SPACE collision/occlusion triangles for a resident chunk with a stable signed X/Z address. `GiWorldGeometryBridge::replaceVegetationProxy` moves it into the existing `GiGeometryScene`; the authoritative foliage/ecology simulation and physics remain unchanged. `GiGeometrySource::VegetationProxy` is separate from authored terrain, live TerraWeave patch and procedural terrain namespaces even with equal chunk coords. The existing `GiGeometryScene::replaceChunk` marks both OLD and NEW bounds when geometry changes, and `removeChunk` marks old bounds on empty/LOD-unload.

Only after the authoritative ECS/wind/PHYSIS read barrier and after all producer outputs agree should the host call `GiWorldPresentationSync::publish(transport)`. GPU rejection from in-flight Metal/Vulkan compute must retain edits/dirty events until a later accepted geometry revision. The active snapshot remains owned by the controller and the retired snapshots must not be released before actual GPU fence completion.

This bridge deliberately does **not** convert every foliage visual mesh into GI triangles: large worlds need low-cost foliage occlusion proxies and bounded resident source sets. Per-call cap 65,536 triangles prevents unbounded single foliage patches; the existing bounded GPU GiGpuSmallScene may refuse well below that (8192 total triangles). The host must choose a suitable proxy/clipmap/CPU fallback; no silent truncation, no fictional GPU capacity.

## What is actually completed / remaining

Completed in source: stable source namespace adapter, no duplicate terrain mesh generator, geometry old/new invalidation support via existing core, source regression case. The renderer's generic ModelDraw/PBR Shader Graph path may sample dynamic GI for foliage model materials when the dynamic atlas is valid and texture slot cap allows; the separate native TerraWeaveGrassRender/TerraWeaveTerrainPatchRender files are NOT present in this experimental branch, so their **actual consumer integration is not done**. Do not call the entire foliage system GI-complete.

Next:
1. Conflict-aware integration with current `main` / TerraWeave render branch. Inspect their actual new material, source ownership and ECS barriers before adding subscriptions; never copy unrelated genre-specific files into engine core.
2. Apply genre-neutral Stage43 custom shader bindings to the live TerraWeave terrain/grass receiving shader variants; preserve full GLES3 fallback where tex21 cannot fit, and validate Metal/Vulkan resource counts.
3. For large worlds, produce a bounded chunk BVH/clipmap GI source selection with tests for 100k+ triangles and moving camera without incorrect silent clipping.
4. Only after functional coverage, run local native CMake/CTest, GLSL ES3/SPIR-V/MSL compiler, Vulkan validation, Metal pixels and GLES device tests, then full acceptance and a SHA-precise test handoff.
