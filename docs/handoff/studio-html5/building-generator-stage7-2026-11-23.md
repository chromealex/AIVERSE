# AIVERSE Studio — Building Generator, stage 7 handoff (2026-11-23)

## Branch, context and ownership

- Working branch: `feature/aiv20261123_building-grammar`, stacked on `feature/aiv20261122_building_styles`.
- Review PR: https://github.com/chromealex/AIVERSE/pull/114 (must remain Draft until focused native, web and GPU verification pass).
- Previous implementation handoff: `docs/handoff/studio-html5/building-generator-2026-11-23.md`.
- Relevant latest code commits: `1cd6ecc21350698df8a211197e8fdd21c8feb4cc` (place/reveal), `1c376fdba6c484a6630966ff65bdf41a8143cbb2` (delayed reveal), `43add932aa515063a4ccfb5df825cc239c19582a` (catalog filtering), `81fe5a75c64188e0471b49cce9092af2bab71f29` (focused CI concurrency).
- Other concurrently authored generator change: `b57d7078df519879523574071794262d06cd17b5` (separate seeded detail RNG to help iterative fitting). Check its CI independently.

## Implementation and APIs

1. `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx` now:
   - Opens the saved project content model in the Assets panel using `studio:reveal-asset`. The event is dispatched after the dock can mount, avoiding a lost event when Assets was closed.
   - Offers **Place in Scene**, routed to existing `studio.assetPlace` with an actual `content/GeneratedBuildings/...` model path and map position (0,0), then publishes the native snapshot through `commitSnapshot` and shows the Scene.
   - Displays the native error if the current project lacks a map entity document or suitable scenery type, instead of claiming a place action succeeded.
2. `apps/studio-web/src/panels/StudioPanelRenderer.tsx` passes the existing `commitSnapshot` callback; no duplicate ECS or scene object store is introduced.
3. `apps/studio-native/StudioBuildingGeneration.h` excludes styles with **no** structural functions from the structural-generator catalog. Facade-only style files remain available to their own authored-building workflows, not as inert choices in this panel.
4. `apps/studio-web/src/components/AssetsPanel.tsx` already listens to `studio:assets-changed` in the current branch; the panel sends that event and the native cross-window `ui.assetsChanged` broadcast after generating a GLB.
5. `.github/workflows/building-style-focused.yml` now has a single PR/manual trigger and cancel-in-progress grouping. Previously duplicate branch push and PR checks built up a queue; this prevents new duplicate focused checks. Existing queued older runs may still be cancelled.
6. The panel still uses the approved shared Studio UI components, native typed bridge and project Asset Catalog. No raw browser filesystem writes, no new renderer and no genre-specific logic.

## Actual verification — do not overstate

- **Previous Stage 4:** focused Linux C++ and GLB tests passed in GitHub Actions run `45210066318` on `97313625`.
- **Stage 5:** `b3d4a196` had focused CI failure at `building_style_tests.cpp:237` because sized mesh fitting failed. New fit residual diagnostics and an independent decorative RNG were added afterward, but a passing solver test has **not** been observed.
- **Stage 6/7:** native Studio asset registration regression, current frontend TypeScript compilation and real 3D preview have **not** yet been confirmed passing for the latest branch. Do not claim they passed because tests exist.
- Native macOS/Windows Studio visual QA, GPU model preview and user screenshots have not run here. No public site/developer diary screenshot was manufactured.

## Validation and immediate next steps

1. Check current `Building Style Focused` and `AIVERSE HTML5 Studio checks` workflow statuses on the latest PR head. For failed jobs, inspect compile/test logs and fix the exact issues.
2. Run `npm --prefix apps/studio-web run typecheck`, `npm --prefix apps/studio-web run build`, `npm --prefix apps/studio-web run test:web`; test narrow dock at 240px and floating-window Assets reveal.
3. Run `tests/studio/BuildingGenerationTests.cpp` (native style catalog, GLB generation, GUID and .meta, duplicate path and traversal checks), and focused C++ building tests/GLB bounds probes. Verify generated models with the real `aiverse_modelview` / native Renderer3D.
4. Fix solver convergence on 24×12×18, 28×15×24 and 32×20×28 across multiple seeds/functions. Preserve the invariant: **no nonuniform rescale** of final mesh vertices, normals or UVs.
5. For production UI, implement real native 3D preview of the 3 candidate seeds with lifetime-managed GPU models, rather than presenting triangle counts as screenshots. Current candidates are real CPU metadata **only**.
6. Preserve `style, function, seed, width, height, depth` in a trusted generation-recipe asset sidecar / metadata for later regeneration, using the existing Asset Catalog rather than an independent file store.
7. Reconcile the old feature base with main and coordinated Studio branches before integration. Do not merge into main before focused tests, native UI and GPU evidence.

Potential release note for 3.1.0 **only after verification/merge**: generate 3D building GLBs in Studio, see them in Assets and place them on the Scene as normal authored entities.
