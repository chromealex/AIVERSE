# AIVERSE HTML5 Studio — Stage 19: native macOS build/test CI

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 4d9b7fbc2f5a65ce40093babb5a35f16f7755d89
Stage commit: see branch history.

## Done
- Added concurrency cancel-in-progress to the Studio HTML5 workflow, avoiding stale CI queues from rapid AI-generated pushes.
- New macos-native GitHub Actions job uses macos-latest with Node 22/Vite, CMake, Apple Metal SDK and the complete AIVERSE submodule checkout.
- Builds opt-in aiverse_studio native host plus StudioSession, viewport layout and terrain geometry tests.
- Runs all three Studio contracts via CTest.
- Updated tools/studio/verify_mac.sh to include terrain geometry contract.

## Changed files
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/19-macos-native-ci.md

## Test evidence
Workflow authored and pushed; status is NOT automatically PASS until workflow runner finishes.
Mac developer smoke test NOT RUN in this connector-only environment.
If the host errors on compilation/linking, inspect GitHub Actions job logs and patch in a new stage, recording exact compiler diagnostics.
Visible Metal/WebView behavior still requires manual QA (CI compiling does not confirm rendering).

## Next
1. Inspect latest GitHub Actions run status and full diagnostics, fix CMake/native compiler errors.
2. Screenshot-check WKWebView + Metal frame / z-order / input on Mac.
3. Complete a generic scene adapter for ECS/world nodes; move beyond current MapDoc primitives.
4. Start migrating inspector, assets, hierarchy and docking capabilities with AI-first components; retain legacy ImGui fallback until parity is demonstrated.
