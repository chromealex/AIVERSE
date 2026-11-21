# Stage 63 — external verification outputs (2026-11-20)

Branch: feature/studio-html5. Implementation: d8d79b2b324e77dd51d5279bb85a66d87c43ef16.

Native CMake accepts AIVERSE_STUDIO_WEB_DIST (legacy default retained). The Mac verification script puts native build, frontend output, TypeScript build info, npm cache and temporary files under /Volumes/KINGSTON/AIVERSE-builds/studio-html5-verify, and refuses an unavailable drive. Bundle validation accepts the same environment variable. Includes web domain tests and material-schema CTest. No runtime, public engine API or save-format change.

Executed from apps/studio-web: npm run typecheck PASS; npm run test:web PASS. First external tsc attempt with -b --tsBuildInfoFile FAILED TS5094; corrected to --incremental --tsBuildInfoFile and PASS. Vite production build PASS with --outDir /Volumes/KINGSTON/AIVERSE-builds/studio-html5-completion/web/dist. Python unittest discover tools/studio test_*.py PASS 19/19 with AIVERSE_STUDIO_WEB_DIST pointing to that output. bash -n tools/studio/verify_mac.sh PASS; git diff --check PASS. Existing untracked node_modules, package-lock and tsbuildinfo preserved.

Native CMake/build, root run-tests.sh, visual WKWebView QA and complete verify_mac.sh: NOT RUN for this stage. CI: not dispatched, URL N/A. Frontend tests do not establish native parity. Remaining disabled authoring controls and partial panels mean this is still an experimental migration.

Next: merge current origin/main into this branch, resolve shared CMake/material/shader contracts, then build native Studio against the integrated source on KINGSTON and exercise project/save/history/docking visually before publishing. Latest inspected origin/main: 076139d12d9ea2380a5445f823323f4e2c096811.
