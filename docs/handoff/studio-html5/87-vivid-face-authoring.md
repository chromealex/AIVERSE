# Stage 87 — VIVID facial authoring


## 2026-11-20 — Studio facial identity authoring, scan/photo reference and parts import

Code: `ff1f660e60742cfc67c7a724c73e892daf0a1e27`, branch
`integration/motionweave-studio-facial-stage184`; preceding core `be6bfcb2e`.
Affected: Studio native binding/session/GPU/build helper, Studio shared-UI
HumanFaceControls and appearance/wardrobe integration, HumanWardrobe independent
rig merge, assembly CLI, native fixtures, docs/vivid-face-authoring.md.

Implemented 18 facial controls, manual local reference-photo points (front XY /
profile ZY), aligned-scan coarse nearest-vertex fit, independent head/body model
merge preserving separate rigs/clips/morphs/materials. Socket inverse TRS chain
retains imported world bind frame while following body. Face applies to fresh
native preview CPU geometry and final assembly before body deformation; source.json
persists recipe. Read-only file import restores recipe against original source.
Native methods humanFaceFitScan and humanHeadBodyImport share existing single
async build status. Optional face field is additive, old appearance/wardrobe callers
remain valid; wardrobe independentRig defaults false. No renderer core changes,
no automatic binary DNA compatibility expansion, no browser project writes.

All outputs on `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`.
Executed verification:
- CMake targets aiverse_studio_human_appearance_session_tests,
  aiverse_vivid_human_wardrobe_tests, aiverse_vivid_assemble, aiverse_studio,
  aiverse_studio_facial_gpu_tests, aiverse_studio_facial_preview_binding_tests PASS.
- Initial make target AiverseStudioHTML5 failed (bundle name, not target); reran
  correct aiverse_studio. Initial binding link failed missing vivid_human; fixed
  target dependency. Initial wardrobe test fixture declared morph name without
  channels; fixed fixture. Logs build-face-integration.log/build-face-studio.log
  retain these failures, later build-face-final-tests.log PASS.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(vivid_face_shape|vivid_human_wardrobe|studio_human_appearance_session|studio_facial_preview_binding|studio_facial_gpu)$'
  --ctest-jobs 1 PASS 5/5; report 20261120-164640-3967.
  Additional translated/nonuniform-scaled socket fixture PASS, report
  20261120-164734-4623. Session tests execute real async face bake/scan/parts import.
- npm run typecheck, npm run test:web (75 tests), npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-face.tsbuildinfo, npx vite build PASS.
  After final recipe restoration UI, typecheck + vite repeated PASS.
  One initial npm invocation at repo root failed ENOENT; reran in studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS 29/29.
- Actual Ada build using optional assembly CLI FACE_JSON: Ada-Face-Edited,
  159396 vertices, 7 meshes, 17 materials, 95831620 bytes, self-contained GLB.
  Recipe full-character/face-ada-test.json (jawWidth .12, cheekWidth .07,
  chinForward .03, noseWidth -.08). Native Creator --smoke --still --portrait
  capture PASS, Metal pipelineFailures=0, pass=1. Inspected
  full-character/ada-face-edited.png and control ada-face-baseline.png.
  Eye/eyelash edge artifacts exist in BOTH old undeformed wardrobe and new face
  preview; NOT clean portrait visual acceptance. No shadows disabled to hide them.
- Latest web dist copied into KINGSTON AiverseStudioHTML5.app Resources/studio-web.
  git diff --check PASS. No CI/push. User authorized tests without locked Mac UI;
  manual WKWebView/narrow panel interaction NOT RUN. Creator image is NOT a Studio
  panel screenshot. Full animated-pose/foreign head asset acceptance NOT RUN.

Limits: photo tool is MANUAL template guidance, not automatic reconstruction,
photogrammetry, detected landmarks or texture extraction. Scan requires prior
alignment/units; coarse nearest vertex, not ICP. No anatomical bone refit, neck
weld, automatic retarget or independent-clip synchronization. Existing MetaHuman
binary DNA importer remains legacy head diagnostic, not full modern body/RigLogic.
No user-specific scan/photo supplied. New tools implement a bounded stage of the
broader request, not full Mesh to MetaHuman parity. Source recipes must NOT be
applied to already baked assets. Photo reference itself is not persisted.
Next: fix existing close-up eye/eyelash rendering; unlocked panel QA; validate
real independent head/body animated assets; automatic scan registration and
photo reconstruction remain separate unfinished work. Keep original source data.
