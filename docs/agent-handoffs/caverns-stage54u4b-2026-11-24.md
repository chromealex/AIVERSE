# Caverns Stage54u4b — native planetary runtime frame

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/docs HEAD `40f7705219ceb2e0aad7519e54839cfd9e8d2ecd`. Main unchanged; compare before implementation reported zero commits behind main. Roadmap 8d477116.

## Changes
- b72b7e9d / 3f95f6d0: PlanetRenderer exposes monotonic last cut epoch even after coordinated clear.
- 2528a8aa / feb04f22 / 7d053b3e: native owner exposes closed/drained proof, residency removes only fully closed owners, collision binding exposes exact baseline compatibility.
- 307a5e57 / 5f53c790 / 9418adc9: `engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h` coordinates existing shared builder, residency, body owners and collision bindings. Observer/pin demand, one in-flight body candidate, bounded global resident volume/region budget, stale candidate rejection, native stage/commit with real host-supplied navigation/GI and reader proof, old coherent publication retained while rebuilding, coverage retirement gated on pins and explicit actual host proof. Retain restored baseline when release proof fails; no implicit deletion of active owners. Explicit restoration generation, retry, admission query and shutdown. Same-body changes requiring incompatible leaf set wait for safe cohort retirement (not seamless arbitrary replacement).
- 0f8088b2: GameView exposes draw callback presence; frame refuses competing owner.
- 27f9a456: exact canonical snapshot comparison without JSON serialization.
- ba5d66ad: existing native physics test includes runtime frame and checks absent hook refusal; prior candidate identity fixture remains. No actual coordinator publication executed.
- ad2c34ca / 40f77052: native example README and existing skill document integration.

## Feature delivery evidence
Implementation/native API: source committed, NOT RUN; no claim of actual gameplay traversal. Architecture/dependencies: shared existing owners, optional native physics, no new ECS density or movement authority. Skill/docs: updated. MCP/Studio/inspector: existing map authoring unaffected, regression NOT RUN; runtime coordinator is not an editable schema. Persistence: existing authority schema unchanged, restore generation API, regression NOT RUN. Graphs: N/A (no new graph domain). Headless: CPU authority unchanged, NOT RUN. Example: native integration recipe, not standalone playable sample. Tests: existing registered physics fixture extended, NOT RUN. Performance/platforms/publishing: NOT RUN, no hardware measurements/screenshots. Exact code HEAD confirmed; workflow query returned no runs. No native compiler or Mac KINGSTON execution. Mandatory merged-tree quick gate pending.

## Remaining
The frame is an opt-in native C++ host integration, not installed in the default spherical GameCore (which still uses surface/grid movement). Host must supply actual exclusion, coverage, global cell/ID allocation, nav/GI preparation/commit/abort/retire and use readiness for admission. No dummy ACKs. A refused coverage change retains old state; pinned cohorts may delay additions/removals. Region readiness currently compares source content and verifies baseline absence; measure cost and optimize at native QA. Runtime destruction uses existing authorized Carve/Fill commands, not arbitrary newly authored surface mouths. Need an actual native host consumer and live end-to-end validation.

Next 54v1: connect PHYSIS acceleration and chart-frame locomotion to the existing motor through generic ECS intent, with no second Transform writer. 54v2: use existing NavMesh/A*/HPA*/Flow providers with native residency and PHYSIS environment revisions, actual publication hooks; then complete host wiring and Stage55 comprehensive native/Studio QA. Do not merge to main or declare full gameplay support yet.
