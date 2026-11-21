# Stage89 — Default facial idle and poses


## 2026-11-20 — Default facial idle and four preview poses

Code `8c71909c5f9e5f69b3b92fae4ad3dddbf7a6458c`, branch
`integration/motionweave-studio-facial-stage184`. User requested default facial
idle and poses applied. HumanPreviewAnimation.h generates preview-only six-second
clips: relaxed(default), A-pose, T-pose, portrait × face on/off. Semantic upperarm/
lowerarm targeting converts world direction into parent bind rotation, updating
matching garment/body skeletons; numeric suffixes/namespaces are recognized.
Small neck/spine motions, blink peaks1.7/4.4s, subtle smile/brow morphs. Uses current
native64 morph capacity, not outdated8-channel restrictions in older AGENTS text.
No import/source edits, rig schema/channel reordering or renderer shader changes.
One existing Studio Animator remains owner; default play/speed1/face enabled,
0.25s pose fade, immediate change while paused. Existing manual facial layer runs
afterwards. Source clips retained but default preview selects generated clips.
Native validated studio.humanPreviewAnimation settings are session-only; no export
or baked animation mutation. Shared Studio UI exposes pose, face toggle, speed,
pause and reset. See docs/vivid-preview-animation.md.

Creator opt-in --preview-pose uses its existing MotionWeave graph. Diagnostic
--preview-time samples a separate pose copy without modifying canonical ECS pose.
IMPORTANT: initial screenshots face-open.png/face-blink.png DID NOT close eyes:
Creator's sparse GPU actor override suppresses the imported Model morph texture.
Fixed preview-pose mode to use ModelDraw.pose through the same standard morph
renderer path as Studio, without actor vertex override. Legacy actor modes remain.
Final capture logs identify actor=MODEL_MORPH. Do NOT cite the early snapshots as
blink acceptance. No second animation scheduler added.

Also corrected prior Studio light/cyclorama positioning to match character preview
normalization (max bind extent→2), rather than raw asset units. No viewport layering
or camera/UI geometry changes. Runtime source model/wardrobe assets unchanged.

Actually executed, outputs under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- CMake aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_studio_facial_gpu_tests PASS.
  build-idle-poses.log, build-idle-checks.log, build-idle-capture-fix.log.
  One Python edit invoked from web directory failed FileNotFoundError before any
  write; rerun at explicit worktree root succeeded. No failed native compile.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(studio_human_appearance_session|studio_facial_gpu)$' --ctest-jobs 1 PASS2/2;
  native/test-reports/20261120-170617-19019, full-character/test-idle-poses.log.
  Fixture tests default enabled state, invalid settings atomicity, paired blink,
  relaxed/T target directions, source immutability, loop endpoint and face-off.
  Existing real Metal GPU fixture passes with default preview clip integration.
- npm run typecheck && npm run test:web (75) && npx vite build PASS. After final
  clothing-intersection hint, typecheck/vite repeated PASS; latest dist copied
  into native app Resources/studio-web on KINGSTON.
- Python UI contracts using KINGSTON PYTHONPYCACHEPREFIX PASS29/29,
  full-character/test-idle-ui.log. git diff --check PASS.
- Actual Creator fixed-time native renders visually inspected:
  full-character/idle-poses/{relaxed-native,a_pose-native,t_pose-native,
  face-open-native,face-blink-native}.png. All five pipelineFailures=0,
  pass=1, actor=MODEL_MORPH. Open t0 vs blink t1.7 clearly shows closed eyelids.
  Default relaxed/A-pose garment placement acceptable in these static front views.
  T-pose exposes shoulder skin through shirt: NOT clean wardrobe visual acceptance.
  Portrait inherited opaque lashes/eye-edge artifacts remain. Gallery.html links
  all final snapshots (lowercase filename gallery.html). These are native Creator
  scene snapshots, NOT screenshots of Studio UI or a real-time video.
- UI interaction, continuous playback pacing, other models/rig conventions,
  cross-platform GPU, full arbitrary animation/clothing collision NOT RUN.
  User still authorizes testing without locked desktop UI. No CI/push/main change.

Known limits: semantic matching is not full retarget/foot IK/anatomical correctives.
Unknown bones/morphs are skipped; original assets have no supplied animation clips.
Default clips are procedural presentation helpers, not licensed mocap assets.
Imported source eye alpha/hair problems from QA remain (do not claim fixed).
Next: fix companion eyelash alpha, shoulder mask/corrective deformation for T-pose,
then unlocked panel switching/pause/idle and continuous blink/orbit validation.
