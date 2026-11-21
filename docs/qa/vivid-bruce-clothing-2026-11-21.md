# Bruce clothing repair — 2026-11-21

Three independent causes were verified:
1. Full-frame no-shadow A/B removes triangle-shaped torso shadow mottling, but
   shoulder/collar spikes remain. Skinned bind matrices agree with source mesh
   placement to <2e-6 in model space. A bind-pose capture is smooth: deformation
   rather than texture corruption causes the spikes when arms lower.
2. Explicit offline Jacobi smoothing of the jacket's skin weights removes sharp
   pose folds. Final fixture uses eight iterations/.8 strength (four/.65 was an intermediate) on welded index adjacency; retain top4
   normalized influences. This is a fixture fallback for missing authored pose
   correctives, not Unreal cloth simulation or RigLogic reconstruction.
3. Covered face/body shoulder skin was visible through the closed jacket. An
   outfit-specific bind-world mask retains upper neck/face and hand-joint regions, with >.75 neck ancestry in the lower neck band;
   indices only,3302triangles removed. Rebuild from unmasked source for other outfits.

An additional shared preview bug was found by Ada regression: complete body pelvis
received weight shift while truncated face pelvis did not. Missing-leg pelvis now
borrows only from complete rigs with the same bind-world frame. Each rig converts
identical world shift to its own parent space; unrelated translated pelvis rejects
borrowing. Extended native test covers same/distant donors and planted feet.
Ada before/after same42frame portrait capture removes broad exposed shoulder patch.

Fixed-PCF shadow handling for SKINNED/BAKED surfaces no longer extrapolates a single
triangle receiver plane across a curved filter disk. Bounded normal offset scales
with filter footprint (1..4 times existing offset). Rigid/cyclorama receiver-plane
correction and contact-hardening PCSS remain unchanged. GLSL/MSL equivalents.
Tests of unconditional derivative evaluation alone did not improve the image;
disabling receiver-plane correction globally exposed the cyclorama: both rejected.
No RHI, ECS or save schema changes. Thin-feature/contact behavior on other hosts
still needs broader visual validation; no universal zero-acne claim.

Files: engine/render/Shaders.h; HumanPreviewAnimation.h; tests/vivid/human_wardrobe.cpp;
new offline smooth_clothing_weights.cjs, mask_covered_skin.cjs and smoothing test;
Creator --preview-bind-pose diagnostic. No new ImGui UI.

Actual checks:
- Creator+Studio+native session builds on KINGSTON: build-verified-final.log,
  build-modular-pelvis.log. CTest wardrobe/face/session3/3 PASS; modular-pelvis
  wardrobe regression rebuilt and passed separately.
- GLSL ES300 + GL330, Q0/1/2 SKINNED/MORPH/NORMALMAP shaders:12/12 glslang PASS.
  Native Metal compiles/captures pipelineFailures0. No Vulkan/mobile GPU test.
- JS smoothing test PASS: normalized influences, rigid invariance, unchanged
  source, malformed input rejection. Actual-asset validate-assets.cjs PASS:
  only jacket skin arrays changed; positions/UV/normals preserved; visibility
  indices are an original triangle subset. Original source glb remains intact.
- approved-motion144frames24fps fixed relaxed idle; selected36/108 inspected.
  Source full-body/collar before, bind/no-shadow, smoothed/masked intermediates
  are retained for reproducibility. Ada synced42frames inspected at blink41.
- Additional torso close-up and T pose checked; first mask left T-pose neck skin
  exposed, corrected with neck-joint ancestry and stronger jacket smoothing. Final
  t-pose8.png inspected without the exposed shoulder/neck patches; video bruce-clothing-approved.mp4
  encoded144frames1500x920. Screenshots are native renders, never retouched.

All outputs: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/cloth-review/.
Source asset path content/Characters/Bruce-Full; manifest backup and corrected model
retain reversible original. This package has no external sculpt CSR/LOD sidecars;
masking can change imported vertex numbering, so do not reuse such sidecars blindly.
No GUI/CI/push/performance benchmark. Next: validate other clothing/poses/material
qualities and future authored pose correctives; keep outfit masks scoped to recipe.
