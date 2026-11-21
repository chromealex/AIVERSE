# Native FXAA and Ada neck diagnostics — 2026-11-21

Branch: integration/motionweave-studio-facial-stage184. Outputs exclusively in
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/neck-review/.

Native Creator and all three Studio native viewport paths now use existing PostFx
FXAA in LDR, Medium quality, with explicit resolve and GPU target retirement.
Creator --no-aa allows A/B. Existing TAA lacks animated-object motion vectors;
this stage does not enable temporal accumulation or claim temporal stability.
Native capture reports AA enabled=1 passes=2, pipelineFailures=0.

Ada's neck mottling persists with shadows off, normal maps removed, and in
unlit albedo-only rendering. Red-head/green-body diagnostic does not show a
body overlap. UV visualization places the visible neck near v=.815-.835.
The opt-in neck_albedo_smoothing importer parameter filters lower face-atlas
color only; upper face, alpha, geometry and normal maps are unchanged.
This is cosmetic source texture cleanup, not a shadow fix or a general atlas
layout detector. Use only with the known Ada/MetaHuman atlas layout.
Source content/Ada/model.gltf now references Ada_head_neck_evenness.png;
original manifest and mapping are backed up under neck-review/source-backup.
The external mh_materials.json records strength1 for reproducibility.

Actual checks:
- Creator + Studio CMake targets built on KINGSTON: build-aa.log,
  build-final.log. Restored shader build: build-restored.log.
- node tools/vivid-dna-import/test_filter_neck_albedo.cjs <tooling>: PASS,
  test-neck-final.log; existing test_bake_materials.cjs PASS test-baker.log.
- Optional bake path matches direct helper output: test-import-option.log PASS.
- CTest postfx/post_fx discovery found zero registered tests; no suite claimed.
- Native captures aa, reviewed (168 frames), clean-no-shadow, source.
  Reviewed frames 0,36,41,110; source reload frame36. Video neck-and-aa.mp4
  is 168 frames at24fps,1500x920. Edges soften; some FXAA softness remains.

IMPORTANT: frame41 still has a sharp lower-neck shadow boundary. Disabling
shadows removes it. This is unresolved. An experimental receiver-plane derivative
change in Shaders.h did not remove it and introduced background seams; reverted.
receiver-blink.png is a failed diagnostic, not final output.
No GUI interaction, Vulkan/mobile parity, CI or performance benchmark performed.
Next: isolate the remaining shadow discontinuity; test real wrinkled older skin
without applying Ada's atlas cleanup to that model.
