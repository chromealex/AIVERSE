# World Processes PR #124 — Studio build and session acceptance

Date: 2026-11-24. Branch `fix/pr124-arm64-acceptance`, child PR #184 against PR #124. Code commit `6f9ef816b`.

Paths: `apps/studio-native/CMakeLists.txt`, `apps/studio-native/tests/StudioSessionTests.cpp`. Linked the owner-history test against its real JSON and SHA-256 owners (`aiverse_engine`, `aiverse_pack`). Repaired Studio fixtures: the planet entity now has an authored `Transform` for stable Studio identity; the linked-reference scenario reuses the previously duplicated endpoint after the earlier delete test; the asset destination is created through the authenticated asset-history operation; the multi-selection assertion checks only selected entities.

Actual evidence on Mac x86_64 / AppleClang 17 / Debug, outputs on KINGSTON:

- `npm run test:web` in `apps/studio-web`: exit 0.
- `npm run build -- --outDir /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/studio-web/dist`: exit 0; TypeScript and Vite production bundle.
- `cmake -S /private/tmp/aiverse-pr124-finish -B /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_STUDIO_WEB_DIST=/Volumes/KINGSTON/AIVERSE-builds/pr124-finish/studio-web/dist`: exit 0.
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native --parallel 4 --target aiverse_studio_session_tests aiverse_studio_owner_history_tests`: exit 0 after the linkage repair.
- `env TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native --output-on-failure -R '^(studio_session|studio_owner_history|studio_mcp_registry)$'`: exit 0, 3/3 PASS. Session test covers Studio world-process authoring, inspector, undo/redo and save/reopen; registry test covers linked operation inventory, not a full live linked process.
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native --parallel 4 --target aiverse_studio`: exit 0; produced `AiverseStudioHTML5.app`.
- `git diff --check`: exit 0 before the code commit.

Feature gate for this bounded Studio repair: build, HTML5 bundle, native Studio tests, authoring/undo/persistence fixture, dependency ownership, and documentation PASS. Real GUI launch, rendered Scene camera/body/live workflow, linked MCP authentication through a running Studio, GPU capture, performance, remaining platform CI and merged-tree Mac quick gate NOT RUN. PR #124 remains draft. Next: inspect ARM64 CI failure, run linked live Studio acceptance and visual capture, then merged-tree Mac gate before integration.
