# Studio Stage 85 — VIVID appearance profiles


## Studio appearance panel and rigid-groom body follow — 2026-11-20

Implementation `994e7c7b41b24916121ceeaefe65f25ea4626cef`, branch
`integration/motionweave-studio-facial-stage184`, parent handoff `77c66ef8e`;
preceding native appearance implementation `fdf3c8d22cede2ca3ff9a382c7f18fdcb081a76a`.

Implemented a dockable Studio UI v1 VIVID Appearance panel (generated through
new_panel.py), 72 native-schema controls: skin19, hair20, lashes2, eyes14, cloth7,
body10. Draft undo/redo/reset, native apply, role assignments, reading a local
recipe, loading sibling appearance.json by checked model GUID, native named
save/load in project/human-looks. Explicit typed methods humanAppearanceInfo,
Apply, Save, Load, List, FromAsset; no arbitrary filesystem/shell bridge and no
browser project-file writes. Native errors/readiness shown separately from drafts.
Pure web validator rejects malformed/out-of-range profile input before rendering
number controls. The native validator remains authoritative.

StudioFacialPreviewBinding keeps a same-model MotionWeave rig while applying
appearance. A new appearance-only mode works without a fabricated facial rig.
StudioFacialGpuPreview applies materials before publishPrepared; fresh generation
ownership, read lease and GPU retirement preserved. Body field runs on the
post-animation presentation matrices. Still one Animator; no simulation, save-
state, GPU ABI, shared Renderer3D shader or batching default changes. CMake links
Studio/GPU test to existing vivid_human. A separate native session integration
fixture checks actual dispatch/load/apply/save/read/stale and symlink rejection.
Project/asset switch clears unrelated appearance. Profile schema/DNA unchanged.

Visual regression discovered and fixed: skin joints followed changed proportions,
but rigid hair/brows did not. applyHumanBody now also warps unskinned mesh-owner
matrices about each mesh's transformed centre (not its often foot-level origin).
Skinned mesh owner nodes remain untouched to avoid double deformation. Added a
rigid attachment numerical regression. Invalid body bounds are caught by both
native hosts instead of escaping the frame loop. Intermediate
ada-look-variant-full is BAD (bald/displaced groom); use fixed-full below.

Affected files: apps/studio-native/{CMakeLists.txt,StudioSession.cpp,
StudioFacialPreviewBinding.h,StudioHumanLookDocument.h,mac/StudioFacialGpuPreview.*,
tests/StudioFacialGpuTests.mm,tests/StudioFacialPreviewBindingTests.cpp,
tests/StudioHumanLookSessionTests.cpp}; apps/studio-web package, registry/types,
generated/HumanAppearancePanel TSX/CSS, humanLookSchema and tests; VIVID
HumanMaterials.cpp, appearance test, Creator main; docs/vivid.md and new
vivid-appearance-controls.md. See git show for exact full paths.

Verification, all builds/caches/assets/logs on KINGSTON at
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/`:
- Final native app/Creator/GPU target build PASS: full-character/
  build-look-publish-final.log. Previous build-studio-look-final.log also built
  the app and new session target; build-look-session-test.log final fixture.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^studio_facial_(preview_binding|gpu)$' --ctest-jobs 1: 2/2 PASS,
  test-studio-look.log. Binding tests include named persistence, path rejection,
  stale controls, same-model rig preservation and appearance-only binding.
- '--filter ^(vivid_human_appearance|studio_facial_gpu)$': 2/2 PASS after rigid
  attachment fix, test-look-rigid-fix.log, report 20261120-154720-98436.
- '--filter ^studio_human_appearance_session$': 1/1 PASS,
  test-look-session-final.log, report 20261120-155139-9875. Initial fixture failed
  because empty gameplay rules had no types; corrected fixture, not production.
- Final '--filter ^studio_facial_gpu$': 1/1 PASS after frame error handling,
  test-look-publish-final.log, report 20261120-155407-14665. Real Metal shader
  submission tests use a small fixture, not a claim of full Ada Studio UI QA.
- npm run typecheck PASS; npm run test:web 75/75 PASS (including 2 profile import
  tests). web-look-checks.log records passing tests plus an initial failed
  `tsc -b --tsBuildInfoFile` attempt (TS5094). Corrected to `npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-look.tsbuildinfo` and `npx vite build`: PASS.
  Final web-look-final-build.log PASS; bundle copied into native app and verified
  present. Existing dist symlink targets KINGSTON. First npm attempt used repo
  root and failed ENOENT; corrected explicit apps/studio-web workdir. Dock tests
  initially exposed generator not adding the default right-stack entry, then
  old two-tab assumptions; registration and fixtures updated, all pass.
- Python UI contracts: 29/29 PASS, test-studio-contracts-look-final.log, using
  PYTHONPYCACHEPREFIX on KINGSTON. git diff --check PASS.
- Real Creator Metal --smoke --still --asset ada-look-controls/manifest.json:
  ada-matte-final-portrait.{log,ppm,png} and --appearance look-variation.json
  ada-look-variant-fixed-full.{log,ppm,png}, ada-look-variant-portrait.*.
  All PASS and viewed, pipelineFailures0; matte cloth/skin, colour changes,
  changed body and following groom observed. Pixel frames precede only the
  final error-catching wrappers, which do not change valid rendering.
- UI tool reports Mac locked. User explicitly replied 'Пока проверяй без
  интерфейса'. WKWebView desktop/240px interaction/screenshots NOT RUN; no claim
  of manual UI acceptance. verify_mac.sh not run literally because it hardcodes
  a repo-local build directory; relevant builds/tests performed at KINGSTON.
  Full engine suite, animation-wide body/clothing/culling checks and Vulkan/GLES
  NOT RUN. No CI/push/main mutation.

Ready external sample: KINGSTON root `VIVID-Look-Lab/content/Ada` with source
companion, `human-looks/Ada-Matte.json`, `Ada-Variation.json`, and launcher
`Open-VIVID-Look-Lab.command`. Project was prepared, not opened interactively.
Choose Panels > VIVID Appearance, select Ada/model.gltf, Load model profile,
Apply to preview. The profile can also be passed to Creator --appearance.
Source assets remain external and are not committed or published.

Limitations/next: perform unlocked Studio interaction and 240px layout check;
validate multiple poses/angles and extreme garment deformations. Body warp is a
bounded Y-up presentation approximation, not anatomical morphs/physics. Hair
colour controls require packed root/seed attributes and do not regenerate hair
geometry. No MetaHuman face texture-index library/region masks, tangent-atlas
anisotropy or full cornea/SSS parity. Control reference documents this explicitly.
Only pre-existing dist/node_modules symlinks should remain untracked after commit.
