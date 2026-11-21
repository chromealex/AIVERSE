# Stage 88 — VIVID studio lighting


## 2026-11-20 — Eight studio lighting rigs and curved cyclorama

Code commit `77e23959c1dcf7157f635c5d9b432c8ed65d7bd6`, branch
`integration/motionweave-studio-facial-stage184`. User requested 8 Unreal-style
lighting presets, multiple sources, curved backdrop. Shared presentation header
`engine/character/vivid/HumanStudioLighting.h` defines eight original rigs:
neutral, portrait, warm, cool, dramatic, rim, gel, catalog. Four sources each:
shadowed directional key plus three High-tier point sources, hemisphere ambient.
64-segment smooth quarter-circle joins floor and wall (134 vertices/396 indices).
Cyclorama and source layout scale/translate to character world bind bounds.
No engine renderer/shader/batching policy changes or experimental flags enabled.

Studio native facial binding owns preview-only settings; validated typed
`studio.humanStudioLighting` gets catalog/settings and optionally sets the whole
settings object atomically. Invalid presets/ranges/types reject without publication.
Light changes do not increment binding revision or reload character GPU assets.
Native Metal Scene owns/releases one immutable unit cyclorama mesh. Facial mode
uses Medium sun shadows and High local lights; map/material modes retain previous
Off shadows/Medium local tier. No map light document or baked texture mutation.
Shared Studio UI component exposes 8 choices, rig yaw [-180,180], gain [.25,2],
backdrop toggle and reset. Settings remain until project/binding reset, not on disk.
Creator CLI `--studio-lighting ID` uses the SAME rigs/geometry for captures;
without option its legacy optical lighting/floor is retained. No new ImGui controls.

Verification actually executed, all outputs on KINGSTON:
- CMake targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests PASS. Initial aggregate braces
  error in new header failed compile; fixed. Original build-studio-lighting.log
  retains failure; build-studio-lighting-retry.log final success.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^studio_human_appearance_session$' --ctest-jobs 1 PASS. Report
  native/test-reports/20261120-165318-10617; test-studio-lighting.log.
  Checks 8 catalog entries, selection, invalid selection atomicity, cyclorama
  vertex/index counts and every triangle winding against smooth normals.
- npm run typecheck, npm run test:web (75), npx vite build PASS.
  dist is KINGSTON symlink; latest dist copied into native app Resources/studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS29; test-lighting-ui-contracts.log.
- Actual native Creator --smoke --still --studio-lighting <ID> with the SAME
  external Ada-Face-Edited asset/camera/pose for all8: PASS8/8, each log has
  pipelineFailures=0 and pass=1. All eight PNGs visually inspected: visible
  distinct light tint/contrast, curved continuous ground/background and sun shadow.
  full-character/light-{neutral,portrait,warm,cool,dramatic,rim,gel,catalog}.{png,log,ppm}
  under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid.
  full-character/lighting-gallery.html links all actual PNGs; no generated/fake
  character images. Left overlay is legacy Creator, NOT new Studio control UI.
- git diff --check PASS. No CI/push. Manual WKWebView/narrow dock screenshots
  NOT RUN: user explicitly asked to keep testing without locked desktop UI.
  Vulkan/GLES and dynamic eight-preset switching visual acceptance NOT RUN.

Limits: original AIVERSE rigs, not copied Epic assets or UE render parity.
Current point lights have no individual shadow maps; no area lights/Lumen/ray
tracing added. Existing eye/eyelash and occasional shadow-edge artifacts remain;
lighting stage does not repair imported material anatomy. Cyclorama is oriented
behind +Z-facing character and fixed in world; rig yaw rotates sources only.
No scene export or persistent project light-preset file. Next: unlocked Studio
controls QA, orbit/rotation acceptance and existing close-up eye material fix.
Build/launch remains KINGSTON/Open-VIVID-Look-Lab.command. No source assets staged.
