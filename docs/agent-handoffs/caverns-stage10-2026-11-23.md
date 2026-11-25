# TerraWeave Caverns — Stage 10 GeometryScene integration handoff (2026-11-23)

## Provenance
Repo `chromealex/AIVERSE`. Branch `feature/terraweave-caverns-core-20261123`, not merged to main.
- `80dfe8f1ea1518a0bbad27100db3a793e9e9ce3a`: new `engine/render/TerraWeaveCavernsGeometryScene.h`
- `6ff5576999679d3f6ce7f9f3c55126442039090c`: new `tests/terraweave_caverns_geometry_scene_test.cpp`
- `6e2c9aec14b46bd16e2dbe3931ff68b6b2ba63d3`: stage-10 update to `skills/world/aiverse-terraweave-caverns/SKILL.md`
- Handoff committed after these; read branch HEAD for latest SHA.

## Existing interfaces actually inspected
- `engine/render/GeometryScene.h` and `GeometryScene.cpp` expose `loadPrepared(gfx::Device&,const Json&,std::map<std::string,std::unique_ptr<Model>>,size_t)`, `draw(Renderer3D&)`, `shutdown(gfx::Device&)`, `setStrataStagingEnabled` and `setStrataPortableRenderingEnabled`.
- `GeometryScene::loadImpl` demands rigid CPU meshes, with nonempty vertex/index streams and **not-yet-uploaded** GPU VB/IB. It owns upload, staging, selection and renderer submission.
- `engine/data/Json.h` supports Json::object/array/number/string/set/push, used to construct the actual scene manifest. STRATA route remains optional; source policy and safety decide whether portable STRATA is selected.
- Do not use `prepareStrataRenderCandidate` as evidence of actual drawing: it only performs preflight, not submission.

## New implementation
- `makeCaveGeometryModel(CaveMesh)` converts chunk-local-metre cave triangles into an actual unuploaded rigid `render::Model`, with rock material and validated finite positions/normals and index range.
- `caveGeometrySourceKey` embeds signed xyz, generator version and revision into a unique source-key string.
- `caveGeometrySceneManifest` builds one-instance Json scene with a world-metre origin from chunk key, resolution, and spacing; STRATA opt-in is explicit.
- `CaveGeometrySceneResident` owns a `render::GeometryScene` and exposes `publish(device,mesh,generatorVersion,revision,resolution,spacingCm,strataEnabled)`, `draw(Renderer3D&)`, `shutdown(device)`. The publish method stages a new scene via real `GeometryScene::loadPrepared`, then replaces the resident after full shutdown. Previous resident is kept until a new stage loads successfully, but global cross-owner atomicity is not provided.
- Added test source verifying source model layout, manifest origin and invalid indices; does not use a graphics device.

## Tests actually performed
- GitHub file create/update calls succeeded.
- **C++ compilation, linked CTest, real GeometryScene upload, STRATA cluster selection/rendering, Metal/GL/Vulkan screenshots, and Box3D/NavMesh gameplay integration were NOT executed this stage**. No runtime success claim.
- New focused source: `tests/terraweave_caverns_geometry_scene_test.cpp`. Register in CMake with required GeometryScene, Model, Json and RHI objects and run native acceptance.

## Important limits / next steps
1. **One GeometryScene per chunk** is a correctness-oriented integration entry point, not a scalable streaming implementation. Batch chunk models into one GeometryScene with incremental source updates, retain stable source keys, and handle retirement fences without blocking GPU idle per-chunk.
2. The same cave mesh must never be simultaneously drawn through GeometryScene and legacy CutTerrainResident; enforce one owning draw route per pass. Texture/material parity and shadow/reflection passes require real runtime tests.
3. `CaveGeometrySceneResident::publish` shuts down previous scene after staging a replacement. It may block/require Device idle. This is NOT cross-system atomic rollback or fully safe no-hitch runtime reload.
4. Real cave ingress still requires publication of terrain hole and cave wall geometry plus authoritative Box3D, SpatialWorld, multi-span native NavMesh, and removal of corresponding original terrain bodies on one owner barrier.
5. STRATA LOD must not change collision/NavMesh geometry; surface cuts, cave seams and mixed-resolution LOD transition tests remain open.
6. Check CMake integration and compile all stage 1–10 new headers/tests, fix API mismatches, run sanitizer and GPU acceptance before merging to main.
7. Continue creating an updated skill plus separate handoff after each stage.
