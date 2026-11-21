# AIVERSE MotionWeave — Stage 181 coordination: facial authoring in HTML5 Studio

**Date:** 2026-11-20
**Animation branch:** `feature/universal-ecs-animation-pipeline` HEAD `a826757ea1ffb994b39ad50c852811f13c8cf5f0` at Stage 181 start. **No core source code changes on this branch in this stage.**
**Editor branch:** `feature/studio-html5`, Stage 67 / MotionWeave Stage 181 companion, completed source handoff commit `287dd2f9fbbfe69196aa31332440cc0ecb1e331d`.
**PR:** https://github.com/chromealex/AIVERSE/pull/22, still draft/unmerged.

## What changed in Studio, not in animation core

The actual user-selected HTML5 editor is `apps/studio-web` with native
`apps/studio-native/StudioSession.cpp` on `feature/studio-html5`.
Previous Stage 180's "editor" direction is therefore fulfilled as a
separate feature branch, avoiding new ImGui code and destructive partial
merges between 1500+ diverged commits.

Studio now has a dedicated dockable `facial_rig` tab using its approved
cinematic navy design system; the unrelated `animation` timeline tab
remains intact. It provides an exact 52-name ARKit-style mapping template,
per-morph gain sliders and semantic node/target controls, half-life,
additive/override settings, basic eye Aim IK authoring, strict frontend
and native schema validation, draft discard and project-local native
`facial-rigs/<name>.json` load/save. Native whitelisted calls:
`studio.facialRigList`, `studio.facialRigGet`,
`studio.facialRigValidate`, `studio.facialRigSave`.
The C++ write implementation stages files with no-follow exclusive
POSIX openat and atomic same-directory rename. No editable browser
arbitrary paths are accepted.

**Boundary:** Studio's schema checks do **not** validate real imported
mesh channels, Model/GPU resource residency or native IK targets. The
published Studio branch does **not** contain Stage 180 runtime
`FacialControlRig.h`. A true 3D live preview is still blocked by the
unmerged runtime; no fake rendered character or synthetic GPU result
was used. Compiled runtime continues to be owned by Stage 180.

## Docs/tests and status

- Source/cross-branch handoff:
  `docs/handoff/studio-html5/67-motionweave-facial-rig-2026-11-20.md`
  (on `feature/studio-html5`).
- Public Studio integration guide:
  `docs/studio-facial-rig-authoring.md` (on `feature/studio-html5`).
- New deferred TS/native fixtures in the Studio branch:
  `apps/studio-web/tests/facialRigSchema.test.mjs`,
  `apps/studio-native/tests/StudioFacialRigDocumentTests.cpp`;
  CTest `studio_facial_rig_document` and `npm run test:facial`.
- **ACTUAL:** GitHub source audit of both branches, SHA-checked code and docs commits/readback, checked 52 unique names. **NOT RUN:** npm typecheck/build/test, native C++ compile/CTest, WKWebView screenshot, GPU Metal/Vulkan/GLES, sanitizers or performance. Source-only: no PASS claim.

## Remaining Stage 182+

Perform a dedicated conflict-aware Studio+MotionWeave integration branch
after both teams finish their current feature work. Link Studio's
`facial_rig` UI to actual `FacialControlRig::compile/apply`, native
ModelAssetProvider and ModelView/Renderer3D, preserving the ONE pose
writer and exact Model/morph generation checks. Add native model/face
asset discovery, per-expression live input and pose preview, full
bone/eye/jaw IK authoring, invalid-morph diagnostics and versioned
per-character binding presets. Then implement automatic morph-
preserving LOD geometry. Run the requested single coordinated native
Mac CMake/CTest/Metal/Vulkan/GLES/sanitizers/crowd/VRAM acceptance
before undrafting PR #22. Do not merge either feature into main solely
on source inspection.

This handoff is a cross-branch coordination note, **not** evidence
that both branches have been merged or that runtime preview works.
