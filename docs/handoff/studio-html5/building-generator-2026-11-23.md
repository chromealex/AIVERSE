# AIVERSE HTML5 Studio: Building Generator — handoff (2026-11-23)

Branch: `feature/aiv20261123_building-grammar`. PR: https://github.com/chromealex/AIVERSE/pull/114
Stacked on `feature/aiv20261122_building_styles`, behind current main.

## Code and API

- `engine/buildings/BuildingGlbWriter.h`: common GLB encoder used by native
  authoring and `tools/buildings/building_style_export.cpp`. Includes
  escaped GLB JSON names and locale-independent number formatting.
- `apps/studio-native/StudioBuildingGeneration.h`: native catalog from real
  style files, CPU inspection of a style/function/seed/dimensions, and bounded
  GLB export into project/content/GeneratedBuildings. No browser-supplied
  filesystem path or executable. Existing files are not intentionally
  overwritten; `StudioAssetCatalog` produces .meta and persistent GUID.
- `apps/studio-native/StudioSession.cpp`: allowlisted methods
  `studio.buildingCatalog`, `studio.buildingInspect` and
  `studio.buildingGenerate`.
- `apps/studio-web/src/types.ts`: DTOs and method typings.
  `src/mock.ts`: native-only mutations explicitly unavailable in browser.
- `src/nestedDock.ts`: dockable `building_generator` panel title/home.
  `src/panels/StudioPanelRenderer.tsx`: registered panel.
  `src/panels/generated/BuildingGeneratorPanel.tsx` + scoped CSS: setting,
  style, function, W/H/D, seed, three CPU candidates, generate, errors,
  real thumbnail if available, and asset GUID/path.
- `src/components/AssetsPanel.tsx`: refreshes when generated asset event
  arrives; no alternative asset store.
- Follow-up panel integration now offers **Reveal in Assets** (waits for the
  dock mount before revealing the real path), and **Place in Scene** via
  existing `studio.assetPlace` creating a normal authored StaticModel
  entity (standard editor Undo/Redo). The native host requires a compatible
  scenery type in rules.json, and rejects unsupported world-space maps.
  Follow-up commit: `1cd6ecc21350698df8a211197e8fdd21c8feb4cc`.
- Catalog follow-up hides facade-only styles from structure selection:
  `43add932aa515063a4ccfb5df825cc239c19582a`.

- `tests/studio/BuildingGenerationTests.cpp`: native catalog, GUID,
  no-overwrite and invalid-input tests. Included in focused GitHub workflow.
- Docs: `docs/building-style.md`, building-style skill, Studio UI guide.
  Roadmap: Studio generator item in progress.

## Tests actually observed

- Previous Stage 4 `Building Style Focused` workflow PASSED on
  commit 97313625; run 45210066318.
- New Stage 5 constructive solver FAILED at `building_style_tests.cpp:237`
  because at least one test envelope does not converge.
  The generator now returns diagnostic `fitError` and residuals, but
  convergence still needs investigation. Do NOT call this fixed.
- Focused native Studio C++/GLB and frontend typecheck/build workflows were
  queued at the latest observation. These have NOT yet been reported passing.
- macOS/Windows native UI, live 3D preview, GPU screenshot, and full CI
  not verified. No public site/devlog screenshot published.

## Exact next steps

1. Check current GitHub Actions results; fix C++/TS integration issues.
2. Tune/repair stage-5 solver and validate arbitrary W/H/D, including
   impossible dimensions, without nonuniformly scaling the final mesh.
3. Real Metal/Renderer3D live preview with proper upload/release lifetime.
4. Open in native Studio on macOS, generate and inspect GLB, GUID and .meta,
   select in Assets, save project; test Windows native host.
5. Integrate old building branch and Studio code with updated main, review
   conflicts from concurrent editor agents before merging.
6. Add real screenshot/site devlog only after native verification.

Suggested 3.1.0 release note **after** verification and main merge:
Building Generator creates real procedural GLB assets from style/function/
dimensions and seed directly in Studio. No save-format migration.
