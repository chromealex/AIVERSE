# Stage 66 — Release assertions (2026-11-20)

Branch feature/studio-html5. Implementation 8108cf0f6c4c9741a71e91024c15c2a71d233332.

Twelve Studio native fixtures now use tests/TestCheck.h AIVERSE_TEST_CHECK instead of assert, so validation and side effects execute with NDEBUG. Existing test semantics retained. Mac verification regex now includes studio_bridge. No product API/save change.

Executed six independent contracts (StudioViewportLayoutTests, StudioLightingValidationTests, StudioMaterialValidationTests, StudioShaderGraphValidationTests, StudioFpsLightingContractTests, StudioLightPreviewMathTests): c++ -std=c++17 -DNDEBUG -Wall -Wextra -Werror -pedantic -I . ... then execute, PASS 6/6. Outputs/TMPDIR on /Volumes/KINGSTON/AIVERSE-builds/studio-html5-completion. Python Studio discovery PASS 19/19 including newly built external bundle. Vite rebuilt with picker change PASS. Integrated native aiverse_studio compile/link PASS; dependent test target build still running, root runner NOT RUN yet. Native build log at same output root/native-build.log. CI N/A, not dispatched. UI NOT RUN, Mac locked.

Next: finish target build, root run-tests.sh --no-build --filter '^(studio_|view_appearance_authoring$)' with explicit external build-dir, then check native user workflows. Keep partial migration status; do not claim full parity or main publication yet.
