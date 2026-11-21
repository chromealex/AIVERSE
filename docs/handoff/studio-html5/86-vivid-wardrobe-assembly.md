# Studio Stage 86 — Wardrobe and character assembly

## Studio wardrobe and safe character publication — 2026-11-20

Implementation `9ade6c85d3f5411bf8bf1fc1f04977598118dda6`, branch `integration/motionweave-studio-facial-stage184`;
prerequisites 4a7f0a3da (morph export) and 3bad4c000 (CPU wardrobe/bake).

Added Studio UI v1 wardrobe controls within VIVID Appearance: real source mesh
visibility, model GUID pickers, slots, enabled items, cloth/source PBR finish,
bone sockets and local TRS, per-item source mesh hiding and body mask texture
GUIDs/material names. Draft undo, native sibling wardrobe.json preset loading,
resolution/name controls, Russian Собрать персонажа button, async status/error,
and loading the completed GLB through existing checked preview binding. There
is NO live per-item wardrobe preview; display the assembled output. No JS project
file writes or new ImGui controls. StudioField labels get actual input IDs.

Native typed methods: humanWardrobeInfo, humanWardrobeFromAsset,
humanCharacterBuild, humanCharacterBuildStatus. A worker captures validated
source/model/mask selections; native FileReader restricts reads to nonsymlink
content paths. Output is content/Characters/name/{model.glb,source.json,
appearance.json}. Exclusive staging, fsync and macOS RENAME_EXCL publish avoid
source/previous-output replacement. Reimport and mesh/skin/morph-name checks
precede publication. The live output appearance is empty to avoid double baking;
source.json retains the editable request. 128 MiB output/import cap; the standalone
Creator cap increased from 64 to 128 MiB to match Studio. aiverse_vivid_assemble
uses the SAME publisher for headless acceptance (content paths instead of GUIDs).
Wardrobe presets normalize optional TRS/finish defaults before returning to TS.

Important body regression found by actual Ada screenshots, despite unit tests:
packed UNORM skin weights sum to 254/256 on some vertices; GPU normalizes by
actual sum. Dividing by 255 during bake produced up to .01556 model-unit error
and severe face/hand artifacts. Fixed by normalized weighted bind/warped matrices,
inverting each vertex's weighted bind, and matching GPU linear skin normals.
Source vertices are snapshotted and only referenced primitive vertices are baked,
preventing repeated/shared-range deformation. Public bakeHumanBody plus tests
cover shared primitives and non-255 weight sums. Body translation keys/pivots
are adjusted; pose-wide anatomical retargeting remains unverified.

Baked Creator manifests optionally support `authoredHair:true` (v1 additive,
false by default), suppressing an extra generated wig WITHOUT replacing baked
PBR. Strict boolean parsing and roundtrip test added. Earlier captures with empty
material roles generated a second wig. Native Studio raw GLB never used that
Creator-only generation path. No core renderer/Metal batching changes.

Affected paths: apps/studio-native CMake/Session/header/StudioHumanCharacterBuild
and session tests; studio-web types and HumanWardrobeControls/appearance panel;
HumanImport, HumanWardrobe, their tests; tools/vivid-creator main/assemble and
wardrobe_fixture.py; docs/vivid.md and vivid-character-assembly.md. No external
MetaHuman/companion data is committed. Procedural fixture accessories are original.

Verification: ALL builds, assets, logs, packages and captures on KINGSTON under
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/. Logs below are full-character/.
- cmake --build native targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_vivid_human_wardrobe_tests,
  aiverse_vivid_human_import_tests, aiverse_vivid_assemble: PASS across scoped builds;
  final build-wardrobe-complete.log. Final app contains built web dist.
- Unified run-tests.sh --no-build --build-dir <KINGSTON>/native --ctest-jobs 1
  filter '^(studio_human_appearance_session|vivid_human_import|vivid_human_wardrobe|
  vivid_human_appearance|modeling_authoring)$': 5/5 PASS, test-wardrobe-final.log,
  report 20261120-161832-58357 (before final body normalization).
- After body/preset fixes, filter '^(studio_human_appearance_session|
  vivid_human_wardrobe|vivid_human_import)$': 3/3 PASS, test-wardrobe-complete.log,
  report 20261120-163045-80190. Earlier shared-vertex and weight regressions also
  PASS in test-body-shared-regression.log and test-body-weight-normalization.log.
  Session tests exercise actual async dispatch, imported defaults/GUID resolution,
  output/recipe creation, duplicate-name failure without partial output, bad GUID.
- npm run typecheck PASS; npm run test:web 75/75 PASS; npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-wardrobe.tsbuildinfo && npx vite build PASS
  (tool output, bundle index-BHaoxO61.js). Initial typecheck caught missing
  StudioField htmlFor and 'texture' instead of AssetKind 'image'; corrected.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v: 29/29 PASS, test-wardrobe-ui-contracts.log.
- Actual Ada builds: Characters/Ada-Wardrobe-Ready/model.glb (160971 vertices,
  9 meshes,22 materials,89316788 bytes); Characters/Ada-Baked-Body-Normalized/
  model.glb (159396 vertices,7 meshes,17 materials,88986864 bytes). Companion
  preset includes separate BlueShirt, SilverWatchBand/dial, GoldChain and
  BodyVisibility.png. Imported garment is a same-rig shirt variant, not proof
  of automatic fitting. Bodies/masks/accessories are all in the final GLB.
- --compare-body PROJECT Ada/model.gltf Characters/Ada-Baked-Body-Normalized/
  model.glb look-variation.json: PASS, compare-ada-body-normalized.log;
  225900 samples, maxPositionError=2.38814e-06, RMS=4.21594e-07,
  minNormalDot=.999165 against live applyHumanBody with GPU-normalized weights.
- Real Metal Creator --smoke --still captures inspected: ada-wardrobe-ready.png
  (shadow48/colour48, pipelineFailures0) and ada-baked-body-normalized.png
  (shadow43/colour43, pipelineFailures0). These are Creator framebuffer captures
  of assembled GLBs, NOT screenshots of the new Studio controls.
- BAD intermediates: Ada-Baked-Variation, Ada-Baked-Body-Fixed (marked DO-NOT-USE),
  ada-baked-variation*.png and ada-baked-body-fixed.png retain dents from the
  pre-normalization implementation. Do not deliver as passing. Initial 93 MiB
  Creator load failed at its old 64 MiB limit. A diagnostic AIVERSE_SHADOWS=off
  did not override Creator's explicit Medium setting (shadow43): NOT a valid
  no-shadow comparison. Temporary override code was removed. Final normal
  shadows are enabled; no rendering defect was hidden by disabling shadows.
- git diff --check PASS. No CI/push/main mutation. Full engine/Vulkan/GLES,
  manual WKWebView 1440/240px and multi-pose wardrobe acceptance NOT RUN. User
  explicitly authorized checking without UI while Mac is locked; no repeated
  unlock/approval request. verify_mac.sh not run verbatim because it hardcodes
  repo-local build output; equivalent scoped builds use KINGSTON.

Ready launch: KINGSTON Open-VIVID-Look-Lab.command. In VIVID Appearance select
Ada/model.gltf, Load model profile, Загрузить гардероб модели, choose a NEW name,
Собрать персонажа, then Показать собранного персонажа. START-HERE.txt updated.
Only existing untracked dist/node_modules symlinks should remain in worktree.
Next: unlocked Studio interaction/narrow dock checks, animation/morph-pose
wardrobe and body tests; live garment preview, LOD/atlas optimization and
automatic fitting are separate unfinished features. Preserve all source assets.
