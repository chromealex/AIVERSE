# Lighting Stage 25 — integrate existing Studio foundation

2026-11-20, integration/lighting-main-stage1-20261120.
Merge code 7f8f3844e3318fd7f74691392458653e8bb2e11d incorporates committed
feature/studio-html5 HEAD 56653e6da. Root checkout/its dirty changes untouched.
User explicitly requested a separate lighting branch, so Studio UI skill's
usual branch preference is overridden by that instruction; no main merge/push.

AGENTS conflict retained both GI/PRISMA and Studio rules. RtsLook conflict kept
shared AppearanceAuthoringService instead of duplicating the old RTS parser;
added inherited shadowDiffusion parsing and regression test to the shared API.
Other shared renderer changes merged additively and passed actual GI pixels.
New AIVERSE_STUDIO_WEB_DIST cache path allows external bundle packaging. npm
build uses tsc --noEmit rather than generating source-tree tsbuildinfo. Worktree
node_modules symlinks to copied existing dependencies on KINGSTON (ignored).
Bundle checker supports an external path and Python3.8 relative_to containment.

Actual validation, all outputs/TMPDIR on KINGSTON lighting-completion-20261120:
- npm --prefix apps/studio-web run typecheck: PASS.
- npm run build -- --outDir <root>/studio-web-dist: PASS; test:web 57/57 PASS.
- Python Studio contracts with AIVERSE_STUDIO_WEB_DIST: 19/19 PASS. Initial
  external bundle check hit Python3.8 missing is_relative_to; corrected without
  weakening containment. No skipped bundle test in final run.
- Debug native aiverse_studio, both HDR runners, Studio session/validation/PBR,
  hdr_policy and appearance authoring targets: build PASS.
- run-tests --no-build --build-dir <root>/build filtered Studio session, material
  validation/PBR, lighting validation, appearance authoring, HDR policy and six
  Metal/GL GI/Luma tests: 12/12 PASS, report 20261120-152651-80473.
- git diff --check PASS. CI N/A. Native UI screenshot NOT RUN yet; this is an
  integration foundation, not completed lighting controls or visual parity.

Native bundle <root>/build/apps/studio-native/AiverseStudioHTML5.app. Build cache
now has AIVERSE_BUILD_TOOLS=ON, AIVERSE_BUILD_STUDIO_HTML5=ON, external bundle
path. Keep other build options. No active command sessions.
Android SDK adb devices -l returned no attached devices; Vulkan loader absent.
Release arm64 GI/Luma seven pixel/native tests passed Stage23 before this merge;
merged Studio Release build/performance not yet validated.

Next: typed persistent renderLighting settings, Studio UI v1 controls, actual
native viewport GI/Luma, native save/undo/reopen and screenshots. Then ICB GI
ABI/acceptance, full test gates and performance. Do not stop at this stage.
