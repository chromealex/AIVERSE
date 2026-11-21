# Stage 90 — thickness-limited backlit SSS (2026-11-21)

Branch: `feature/studio-html5`. Final implementation commit:
`607f7b958`. An earlier concurrent Git operation temporarily moved the
branch after SSS files were staged, so the initial handoff commit
`5f2a94ead` preceded this final implementation commit. This correction
records the durable code SHA; no peer history was rewritten.

## SSS code and behavior

- `render::Material::sssThickness` is an optional maximum light path in
  world units, 0..1. Zero preserves Stage 88's artistic backlight. A positive
  value activates thickness-limited transmission through the existing
  directional sun-shadow depth texture. The shader compares light-entry depth
  with the visible fragment depth, converts the difference to world distance
  and exponentially attenuates backlight. It rejects missing/out-of-range
  shadow depths and paths outside the material limit. This is a single-pass
  approximation: no added texture, new RHI binding, uniform size change,
  PRISMA pass or SpectraFX dependency.
- New field goes through `Model.h`, GLSL/MSL `ModelShaders.h`,
  `Renderer3DModels.cpp` packing (PBR-only `light.w`; unlit fixed-light
  semantics preserved), GPU state equality/hash, material palette and static
  geometry comparisons, `AppearanceAuthoringService.h`,
  `MaterialOps.cpp`, Studio native validation/preview and the HTML5
  Materials schema. `docs/models.md` includes a meter-scale hand example.
- Tests updated in `tests/render/model_batching_policy.cpp`,
  `tests/view/appearance_authoring.cpp`,
  `apps/studio-native/tests/StudioMaterialValidationTests.cpp`,
  `StudioPbrMaterialPreviewTests.cpp` and
  `apps/studio-web/tests/materialSchema.test.mjs`.
- `tests/render/HdrScene.h` adds `AIVERSE_SSS_THICKNESS_TEST` and
  `AIVERSE_SSS_BACKLIGHT_TEST`. The latter renders a thin ellipsoid and
  a thick sphere with the sun behind them.
  `tests/render/verify_sss_backlight.py` checks thin and thick center
  pixels and Metal/GL agreement. Actual Metal images:
  [off](assets/sss-backlight-off.jpg), [on](assets/sss-backlight-on.jpg).

## Compatibility and limits

Existing JSON remains valid and the default `sssThickness=0` retains Stage
88 appearance. Existing unlit uniforms and the 36-float U2/Metal ICB ABI
are unchanged. With positive thickness, the material must cast and receive
sun shadows; a disabled/unavailable shadow map suppresses the transmission
term. The sun shadow map has no object ID, so very close overlapping
occluders can be misread as the material's light entry. Shadow resolution
also limits thin-detail accuracy. Studio's native Materials preview currently
uses `ShadowQuality::Off`, so its sphere does not show thickness-limited
transmission even though the actual Scene/game renderer does. No true volume
transport, thickness texture or point-light SSS was added.

## Tests actually run

- `cmake --build /private/tmp/aiverse-shadow-noise-20261120 --target
  aiverse_hdr_scene aiverse_hdr_gl -j 4`: PASS.
- Real Apple M3 Ultra Metal and GL33 offscreen captures with
  `AIVERSE_SSS_BACKLIGHT_TEST=1`, strengths 0 / 0.35 and
  `AIVERSE_SSS_THICKNESS_TEST=0.2`: PASS; Metal reported zero pipeline
  failures. Captures saved to `/private/tmp/aiverse-finger-{metal,gl}-{off,on}.ppm`.
- `python3 tests/render/verify_sss_backlight.py` on those four captures:
  PASS. Thin center gain: Metal 22.16, GL 22.14 (8-bit values);
  thick center gain: 0.00 on both. Metal/GL mean channel deltas:
  0.161 off, 0.162 on. Manual inspection confirms thin body glows
  while thick center remains dark.
- Earlier non-backlit capture with `sssThickness=0.08` on the Stage 89
  sphere gallery: Metal and GL shader runs PASS; it barely changes that
  scene, confirming why the dedicated backlit fixture is necessary.
- `python3 tests/render/check_material_palette_shaders.py`: PASS,
  144 linked GLSL 330/GLES 300 variants.
- `npm run typecheck`, `npm run build`, `npm run test:web`: PASS.
- `cmake --build build/studio-html5-verify --target aiverse_runtime
  aiverse_studio_material_validation_tests aiverse_studio_pbr_adapter_tests
  aiverse_appearance_authoring_tests aiverse_model_batching_policy_tests
  -j 4`: PASS.
- Focused CTest `studio_material_validation|studio_pbr_adapter|
  view_appearance_authoring|model_batching_policy`: PASS, 4/4.
- `git diff --check` on SSS paths: PASS.

NOT RUN: real imported hand/skin asset, interactive Studio viewport,
Vulkan or WebGL runtime, GPU performance A/B/A, full native suite, CI.
CI URL/status: N/A.

Next: validate on an imported closed hand mesh with meter-scale dimensions
and sun behind the fingers; tune `sssStrength` around 0.15–0.35 and
`sssThickness` around 0.03–0.08. If the shadow-depth approximation
confuses nearby fingers or lighting, add an authored thickness/coverage
mask as a separate stage. The Studio material preview needs an explicit
backlight/shadow preview mode before its sphere can display this option.
