# Building Graph / Caverns integration — Stage 3

Owner: integration/building-caverns-20261124. Code commit: c0c477d27.

Merged `origin/integration/caverns-studio-stage54c-20261124` at 652a7102a into the Building plus guarded-graph staging tree. Resolved three conflicts: kept the existing region ECS test registration instead of duplicating it, included the native Caverns test matrix after texture graph tests, and retained all Building Studio methods while adding `studio.cavernsPreview`. The region ledger test kept its existing equivalent assertion. Ported the Stage 55 shared inspection mounts into the current Caverns panel without replacing its newer authoring controls. Native `studio.cavernsGraphInspect` now reads the current owner planetary report through the scoped handler.

Verification on this stage: `git diff --cached --check` exit 0; Mac CMake configure using Unix Makefiles and `-DAIVERSE_BUILD_TESTS=ON` exit 0 in `/Volumes/KINGSTON/AIVERSE-builds/building-caverns-integration-make`. Native targeted build was started and is still in progress at handoff creation. TypeScript, native tests, live MCP, visual QA and full Mac quick gate are **NOT RUN**. No main merge is authorized yet.

Next: finish the targeted build and tests, fix any merged-tree failures, run Studio and full Mac acceptance, update manifests and handoff with exact results, then submit one squash integration into main under MAIN ownership.
