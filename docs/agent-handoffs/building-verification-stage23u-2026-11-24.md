# AIVERSE Buildings — Stage 23U: initial executable verification coverage and CI launch

Date: 2026-11-24
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114
PR base at observation: feature/aiv20261122_building_styles (not main)

## User mandate and scope

User changed the prior feature-first/test-later decision to "Делай" and authorized actual verification. Target all 12 visual styles, PBR/GLB export, native HTML5 Studio, shared ECS/PHYSIS and Renderer3D. Changes and tests remain independent of deprecated FPS/ActorHost. Keep Draft PR unmerged until results are actually verified and regressions fixed.

## Existing CI and environment discovered

Current GitHub repo has:
- .github/workflows/building-style-focused.yml: focused Linux C++20 tests, standalone GLB exporter, Studio asset generator acceptance, real GLB2 structural checks and artifacts.
- .github/workflows/studio-html5-web.yml: Node 22 TypeScript + Vite + web unit tests, macOS Metal/Studio native compiler contract tests, Windows WebView2/Vulkan native build, headless checks.
- CI workflow for Linux, macOS/iOS, Windows, Web and other platforms.
- GitHub Actions runs were successfully triggered automatically by commits to PR #114. Example latest observed jobs were queued, with NO completed test conclusion. Do not claim queued CI has passed.
- Local isolated execution environment contains clang++, g++, CMake, Node, but it has no checkout of this private AIVERSE repository and local network git ls-remote failed to resolve github.com. The connected GitHub tool can fetch source files and create commits, but cannot materialize an entire native build or execute GitHub workflow jobs on demand. Actual CMake, native Studio, TypeScript and GPU runs therefore depend on GitHub-hosted runner availability.

## Checks actually executed now

1. Read all 12 authored engine/assets/building-styles/<id>.json via GitHub and parsed them as JSON using JavaScript. Ran 623 structural contract comparisons across style identity, 19 material records/style, 0..1 metallic/roughness ranges, 16 supported surface pattern vocabulary, known massing and structural form kinds, referenced material IDs and three palettes/style. 623 comparisons; 0 discovered issues. These static checks do NOT execute the C++ parser or GLB generator.
2. Inspected current native Studio C++ producer/preview and StudioHost API. Found unconditional StudioAssetCatalog::rebuild() during temporary preview, which updates SQLite for an action described as zero generated asset writes. Replaced with lazy StudioAssetCatalog only when an imported texture GUID is requested, eliminating unconditional catalog rebuild. Note: a lazy catalog constructor can still create cache database files for referenced GUIDs; this stage does NOT guarantee absolutely no workspace metadata side effects.
3. Found a generated-thumbnail path mismatch: native StudioHost::assetThumbnail requires a path starting with content/, while studioBuildingGenerate returns paths relative to content/. Fixed BuildingGeneratorPanel.refreshGeneratedThumbnail to prefix content/ before invoking the native thumbnail API.
4. Inspected existing data/API connections of preview, both RPCs, browser type declarations/mocks, StudioScene, and Model batching policy. One simplistic text check for GeometryPoolPolicy::Dedicated in the Model header was a false check (the Model header correctly declares the enum, the Dedicated value is selected in the building adapter), not a source defect.

## New executable regression coverage committed (not yet run)

- tests/libraries/building_surface_pattern_tests.cpp: standalone portable C++20 test of 16 deterministic generated surface patterns, three PBR channels, seed changes, RGBA size/alpha and invalid style rejection.
- tools/buildings/verify_setting_glb.py: Python3 stdlib validator for all twelve real GLB2 generated models. Requires valid JSON+BIN chunks, correctly bounded embedded PNG image buffer views, 128x128 PNG IHDR and all three PBR texture channels.
- .github/workflows/building-style-focused.yml: now compiles and runs the new standalone C++ test, exports a residence structure with seed 17 for all twelve authored setting files, validates actual emitted GLBs with the Python validator, and uploads generated models for visual QA.
- tests/studio/BuildingGenerationTests.cpp: native Studio test now requires all twelve styles/neutral structure presets, checks physicalSockets emitted alongside new GLB recipes, and checks listing/reloading an authored project recipe through the native safe reader.

These are source-level test programs and CI steps pending a runner. No PASS claimed until full job logs and artifacts are available.

## Changed files and commit provenance

- 02ee0eedc3978e2566a40107e014b43e685867b1 — lazy Assets index for temporary native building preview.
- c5ffad0f7c9e52483af88a436345d4f2ff5c8d64 — correct thumbnail content/ path.
- f132664c48859c973a9e186b2edd0eae7a5710a4 — portable PBR pattern C++ regression test.
- 86a46e841bb8b81b836f30e8528acc44dcf3e792 — GLB2/PBR PNG validation script.
- bc96a4ca1bec855b7c327a0354088fbaad53693e — focused C++/GLB CI workflow extension.
- d2ad7bb8e5294e09cf1167ad138bf8ab8420ced4 — native Studio catalog/recipe/physical socket coverage.
- 7e6d28174ea2fc2d4f413925d3b3554363d7b80b — parent building skill update.
- This is the mandatory separate Stage 23U handoff documentation commit.

## Actual testing and incomplete verification

No native CMake/CTest, Linux/macOS/Windows C++ build, TypeScript npm typecheck, Vite build, Studio native GUI session, GLB exporter executable, Python GLB validator on actual export, Metal/Vulkan/GLES screenshot, Box3D PHYSIS motion, rollback simulation, complete NavMesh rebuild or GPU retirement test has completed in this chat. GitHub jobs were queued; no actual pass/fail results were available from them. User should not be told the full feature is verified or ready to merge.

## Next agent exact actions

1. Obtain latest head_SHA of PR #114 and its GitHub Actions workflow runs. Open Building Style Focused, HTML5 Studio checks, and CI jobs. Wait for actual GitHub-hosted runner completion outside this chat if still queued; do NOT invent future results. For failed jobs, fetch job steps/logs and fix exact compiler/runtime errors in feature/aiv20261123_building-grammar.
2. Building Style Focused: compile new building_surface_pattern_tests.cpp with -Wall -Wextra -Werror; run it and then the all-12 exporter & verify_setting_glb.py. Inspect actual generated GLB artifacts in an authentic native model viewer; check UV and lighting.
3. HTML5 Studio: run npm install, typecheck, test:web, build; compile native macOS Metal and Windows WebView2 hosts; run actual Studio building preview start/replace/stop, saved recipe edit/reload, physical sockets, PBR material GUID resolution, GPU resource retirement and camera controls. Verify no stale texture handles, no invalid imported image, and safe viewport/project teardown.
4. Common ECS/PHYSIS: authoritative paired collider obstruction, no stale graph ACK, native NavMesh invalidation+refresh and accurate rollback rebuild. Add explicit test cases for half-open obstruction and moving leaves across scene unload.
5. Reconcile base/main changes: current Draft PR base is a feature branch, NOT main. Do not force merge or mark ready before appropriate integration and full platform acceptance.
6. Update agent skill and create a new handoff after each concrete fix/verification batch; publish site devlog only with real engine screenshots from passing native runs.
