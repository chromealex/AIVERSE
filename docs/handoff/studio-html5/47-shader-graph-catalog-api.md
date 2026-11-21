# AIVERSE HTML5 Studio — Stage 47: Shader Graph catalog and safe node helpers

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 25ae6eb3b12a4dd7c678906f3e411272b82442b1
Commit: see branch HEAD.

## Implemented
- Read-only studio.shaderGraphCatalog method in StudioSession, delegating to the native Shader Graph op catalog and exposing only known operation IDs, input counts and supported domains (capped at 256).
- Typed frontend DTO, mock fallback and negative no-project contract.
- Pure helper addShaderGraphNode inserts one bounded node with unique ID/default references; removeShaderGraphNode rejects deleting nodes referenced by other node inputs or outputs.
- Added Node 22 unit test cases for creation bounds, source immutability and reference-preserving deletes.

## Files
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/panels/shaderGraphLayout.ts
apps/studio-web/tests/shaderGraphEdit.test.mjs
docs/handoff/studio-html5/47-shader-graph-catalog-api.md

## Actual validation
GitHub source committed. Full C++ StudioSession, React/Vite and macOS build NOT RUN. Existing Stage 46 graph helper tests were run locally and passed 5/5; the additional Stage 47 tests are authored but require a new run. GitHub CI remains pending.

## Next
Connect catalog dropdown and add/remove controls to HTML5 ShaderGraphPanel in Stage 48, retaining compiler Validate and explicit Save. Avoid auto-saving drafts. Add handoff.
