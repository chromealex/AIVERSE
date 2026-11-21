# Stage 88 — opt-in PBR subsurface approximation (2026-11-21)

Branch: `feature/studio-html5`. Implementation commit:
`8dac0a25645bef3f543807d5d503c6f4e7697efc`.

## Changes

- `render::Material` adds `sssStrength` (default 0), `sssColor` (linear RGB,
  default white) and `sssScale` (default 0.5). The built-in PBR fragment
  shaders for GLSL and Metal add tinted diffuse wrap and backlit response from
  the sun. Specular remains unchanged. No PRISMA screen pass or SpectraFX work.
- `Renderer3DModels.cpp` packs these values into the PBR-only uses of existing
  U2 `uv1.zw` and `light.xyz`; unlit fixed-light data retains its meaning.
  U2 size, ICB descriptor layout and texture bindings are unchanged.
- Material GPU equivalence/hash, rigid material palette comparison and static
  geometry comparison include the new fields. JSON material authoring and
  inheritance, Studio native preview and HTML5 Materials controls support them.
  Studio accepts comma-separated linear RGB and native validation saves the
  canonical `sssColor` array.
- Affected paths: `engine/render/{Model.h,ModelShaders.h,Renderer3DModels.cpp,
  GpuMaterialState.h,ModelMerge.cpp,GeometryScene.cpp}`,
  `engine/view/AppearanceAuthoringService.h`,
  `apps/editor/core/MaterialOps.cpp`,
  `apps/studio-native/{StudioMaterialValidation.h,StudioPbrMaterialPreview.h,
  StudioSession.cpp}`, `apps/studio-web/src/panels/materialSchema.ts`,
  their focused tests, and `docs/models.md`.

## Compatibility and limits

Existing materials render as before because `sssStrength=0`. Existing JSON
save keys are preserved; the three new keys are optional. Native C++ consumers
must rebuild for the extended `Material` struct. No thickness texture,
geometry-derived thickness, volume transport, point-light SSS or glTF SSS
extension import was implemented. Backlit response is artistic and does not
prove light transmission through the mesh. No visual-quality or GPU performance
claim is made without target-device captures.

## Checks actually run

- `npm run typecheck`, `npm run build`, `npm run test:web`: PASS.
- `cmake --build build/studio-html5-verify --target aiverse_runtime -j 4`: PASS.
- Builds of `aiverse_studio_material_validation_tests`,
  `aiverse_studio_pbr_adapter_tests`, `aiverse_appearance_authoring_tests`,
  `aiverse_model_batching_policy_tests` and `aiverse_studio_session_tests`: PASS.
- Focused `ctest --test-dir build/studio-html5-verify --output-on-failure
  -R '^(studio_material_validation|studio_pbr_adapter|studio_session|
  view_appearance_authoring|model_batching_policy)$'`: PASS, 5/5 after final
  source rebuild.
- `git diff --cached --check`: PASS before implementation commit.
- `python3 -m unittest discover -s tools/studio -p 'test_*.py' -q`:
  FAIL, 40/41. `test_reference_pin_zoom` requires an exact string absent
  from `StudioMetalScene.cpp` already at the pre-stage HEAD; this stage did
  not edit that file.

NOT RUN: real Metal/GL/Vulkan pixel comparison, interactive Studio screenshot,
whole native CTest suite, CI. CI URL/status: N/A.

Next: use a real thin model and skin/wax materials in the native viewport;
compare SSS 0 versus enabled on Metal and GL, inspect backlit occlusion and
performance, then tune or add a separate thickness-map stage if needed.
