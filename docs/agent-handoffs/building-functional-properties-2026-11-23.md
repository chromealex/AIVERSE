# Handoff — AIVERSE Building Functional Properties, Stage 10 (2026-11-23)

Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)
Source revision before handoff: 2e22a07059ccf749d9432b211346f894c6fba8f6

## Implemented

Functional building recipe is independent of visual style, setting and
legacy structure role. User-specified properties include large-gate,
small-gate, personnel-door, sliding-roof, with real metre-sized geometry,
counts, side and open fraction. New genre-neutral CPU implementation:
libraries/include/aiverselib/systems/BuildingFeatureGenerator.h.
Real segmented walls, wall collision openings, a roof slab with a real
aperture, physical sliding roof panels and semantic access anchors.

Portable runtime:
libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h now provides
generateRecipe and composeFeatures request mode, including featureAnchors.
Style registration accepts palette-only/facade-only styles.
No Studio/GPU/filesystem/genre dependency in the runtime geometry path.

Native and UI:
apps/studio-native/StudioBuildingGeneration.h supports mode=features and
records canonical editable .glb.recipe.json next to each generated GLB.
apps/studio-web/src/types.ts and panels/generated/BuildingGeneratorPanel.tsx
support functional properties and legacy silhouette mode.
CSS uses the common Studio UI palette/tokens; all new labels bind to input IDs.
tools/buildings/building_style_export.cpp accepts --recipe, usable with scifi
or modern style JSON. Example: engine/assets/building-recipes/multiuse-open.json.

Acceptance:
tests/libraries/building_feature_tests.cpp checks real free apertures,
shutter and roof open/closed collision, styles, yaw, deterministic mesh
and malformed recipe failure. CMake target library_building_features added.
tests/studio/BuildingGenerationTests.cpp includes both-style export and
recipe sidecar verification. Building Style Focused CI compiles and runs
new feature tests and exports the example in two material styles.

Notable commits:
1c2f5fec (feature geometry), 1684db00 (runtime), 80912f80 (style
compatibility), d832b4e6 (seed precision), f8718e15 (Studio native),
2e0098f7 (UI types), d89a0d2b (UI), 42ea6dc2 (CSS),
aa0128bf (--recipe CLI), 45a4cb00 (example), 8cdefee9 (tests),
74d84d34 (CMake), 79278956 (CI), 93c8f6c0 (roof slide),
acadce02 (field accessibility), f2971411 (Studio test),
6fb256b0 (GLB node name), 4a800f6c (runtime feature anchors),
4169243e (canonical sidecar), d6be4f2f (UI sidecar type),
9638ed1b (UI sidecar), 2e22a070 (sidecar acceptance).

## Actual QA evidence

Earlier 20-size legacy CLI run provided by user: 19/20 PASS, one
factory 16x9x11 seed3 failed. This does NOT validate the new recipe route.
New functional feature tests, native Studio TS/Clang, real Metal modelview,
game-build GPU lifecycle and navmesh/physics state integration have NOT
been confirmed passed on this revision. Keep PR Draft. No new real
screenshots have been taken for Stage 10.

## Limits / next agent

Apertures are real only on the new functional shell path. Legacy sci-fi
preset hulls are closed with visual gate overlays. No fake enterability
claims. Open fraction currently generates a new CPU mesh; production
animation and runtime collider/navmesh mutation need existing owner
threads and safe resource retirement, not CPU geometry every tick.

Next: run focused Clang/GCC C++ tests and Studio TS check, fix failures;
export same recipe with scifi and modern, inspect actual GLB on Mac;
add arbitrary placement, windows, ramps, garage shutters, roof pads,
then integrate the functional shell with varied massing and ECS components.
Do not merge into main before native Mac verification and reconciled
parent branches.
