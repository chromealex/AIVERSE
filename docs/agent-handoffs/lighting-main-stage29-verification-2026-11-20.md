# Lighting Stage 29 — integration verification

2026-11-20; branch integration/lighting-main-stage1-20261120.
Implementation: 6ce5b19c061ec16a450f60bc475e0e9f6e6674fe.

Restored two merged legacy SpectraFX preview calls to the existing shared
ViewportGpuRetirement API (GPU Assets and Graph windows). No VFX behavior or
new ImGui UI. Repaired Studio UI skill frontmatter, escaped Markdown and editor
workflow/category registration; regenerated skills/catalog.json. Added opt-in
Release arm64 OFF/ON/OFF lighting acceptance runner and updated lighting docs
and AGENTS checkpoint. No save-format or public rendering API changes.

All builds, dependencies, temporary data and logs reside at
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120.
Actual checks:
- Release arm64 aiverse_renderbench, aiverse_hdr_scene, aiverse_studio,
  aiverse_studio_metal_lighting_tests and aiverse_client builds PASS.
- Release CTest lighting GPU/host/ICB/animated selection: 10/10 PASS (27.87s),
  logs/stage29-release-gpu.log and .xml.
- Release studio_metal_lighting: 1/1 PASS (4.95s), stage29-release-studio.log/xml.
- Debug full build including AiverseEditor PASS after fixing the two calls.
- Full ./run-tests.sh --quick --build-dir <root>/build --jobs 12 --ctest-jobs 2
  still RUNNING at handoff, report 20261120-155447-18337, session 24647.
  Main aiverse_tests passed in 840.79s. Three catalog/routing/audit tests failed
  before the metadata fix; rerun through CTest after completion is required.
- catalog.py generate/check, tests/skills/catalog_test.py, tools/skills/audit.py
  PASS after fix (227 skills, zero audit errors/warnings). Python syntax check
  of acceptance runner PASS; performance runner NOT EXECUTED yet.
- Earlier own low-parallel build processes were terminated and resumed at 12
  jobs; first resumed Debug build failed on the two Viewport calls, then fixed.
  Interrupted/failed attempts are not passes. Logs stage29-quick*.log and
  stage29-release*.log preserve evidence.
- git diff --check PASS. CI N/A, no push.

Locked Mac still blocks interactive HTML/native screenshot QA. User confirms no
Android/Vulkan hardware stand presently: hardware acceptance NOT RUN. Native
Metal capture tests work independently. Experimental ICB stays OFF by default;
Studio geometry remains terrain/proxy preview, not full game asset parity.
Next: fix FPS preview/GI bounds and camera using actual geometry rather than
MapDoc default RTS dimensions, verify actual Metal; then consolidate full-suite
results and run isolated Release Sponza OFF/ON/OFF acceptance.
