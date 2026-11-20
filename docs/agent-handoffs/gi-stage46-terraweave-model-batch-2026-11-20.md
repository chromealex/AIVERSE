# AIVERSE GI — Stage 46: TerraWeave grass / terrain ModelDraw palette GI (2026-11-20)

## Source commits / branch / verification

Repository `chromealex/AIVERSE`, branch `feature/gi-lighting-portable-backends-20261119`, draft PR #47 targeting `feature/hybrid-dynamic-gi` (NOT merged to the now-diverged main).
- `7e1c42781d33dc5ad7041f840450b4d76848ae00`: `engine/render/GiModelBatchPolicy.h`, pure genre-neutral admission policy; CPU indexed/MDI per-instance material palettes do not conflict with GI U6/tex13.
- `a82129194fe0dc880f228c91e0ca59dc5c919490`: `engine/render/Renderer3DModels.cpp`, remove unnecessary `instancePalette` rejection from dynamic GI selection while preserving GPU-authored/ICB exclusion and all quality, shader graph, HDR, fragment cap and unlit gates.
- `fec3094bb4e28a240d61fc9c32a34b5fc8a6e5c8`: `tests/render/gi_model_batch_policy.cpp`, compile-time batch admission assertions and model GLSL/MSL source coexistence strings.
- `6b3983a4840de6591462d3f4d9697d247d47117a`: CMake/CTest registration for `gi_model_batch_policy`.
**Native compiler, CTest, shaders and GPU pixel/performance tests: NOT RUN. Source committed only.**

## Findings from current main branch (read-only review)

As of current main inspection, `engine/render/TerraWeaveTerrainPatchRender.h` has `TerrainPatchResident::submit` constructing an ordinary `render::ModelDraw` and passing it to `Renderer3D::drawModel`; `engine/render/TerraWeaveGrassRender.h` does exactly the same for patch-batched grass with `kDrawStatic|kDrawNoCastShadow`. `engine/world/TerraWeaveTerrainRuntime.h` calls these two producers from `TerrainMutationCoordinator::draw`. Those files are **NOT PRESENT in this isolated GI branch** and were **NOT copied/cherry-picked** from main (which has 1,000+ unrelated new commits). Existing `Renderer3DModels.cpp` already owns one generic PBR model fragment path for them, with `DYNAMIC_GI` permutations. It is not appropriate to invent a second TerraWeave-specific fragment shader or include the TerraWeave API in common engine code.

Previously `Renderer3D::dynamicGiEligible` rejected `instancePalette` (a CPU-built indexed multi-draw path sharing PBR palette U3), even though GI uses completely disjoint U6/tex13+ slots and is accumulated after per-instance PBR material factors. Thus dynamically lit patch/grass models could lose GI when batched in that mode. Stage 46 permits the CPU-built palette path and leaves the special Metal GPU-authored instance-buffer/ICB mode disabled until its texture-binding contract is proven.

This is **source-level feature coverage**, not validated visual parity or proof that every TerraWeave grass patch is visible, GI-bound, opaque, appropriately shadowed, and included in the BVH. Per-chunk vegetation **occluder** publication is independently handled by the Stage 45 `GiWorldGeometryBridge::replaceVegetationProxy`; per-instance PBR GI **receiver** is the ModelDraw path here. No duplicate per-blade ECS entities or rendering is added.

## Next steps

1. Native compile/run `gi_model_batch_policy` and renderer linked model batching tests on Mac/Metal and Linux Vulkan; exercise a patch-built and grass-built ModelDraw with/without GI, with CPU MDI palette, normal instancing and GPU-authored-disabled path.
2. Integrate the actual main TerraWeave sources **conflict-aware** with this GI branch when the other agents finish; do not manually copy current main headers, and verify `Renderer3D::drawModel` gets the same bound GI atlas between `beginScene` and `render`.
3. Add a producer-side event adapter for `TerrainMutationCoordinator::pendingTerrainNotifications()`, `residentCells()` and immutable `presentationSnapshot()` after publisher barriers. Do not consume `takeTerrainDirty()` before navmesh/Horizon listeners or accidentally rebake twice.
4. Validate real large-world BVH/clipmap budgets, GLES terrain texture constraints and Metal/Vulkan pixel parity; finish all functions, then one full test suite and review conflicts before PR main merge.
