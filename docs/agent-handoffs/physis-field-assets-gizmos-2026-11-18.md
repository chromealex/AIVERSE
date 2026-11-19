# PHYSIS Stage 19 — authored field assets, scene gizmos and native ECS spawn (2026-11-18)

**Repo:** `chromealex/AIVERSE`. **Branch:** `feature/physis-nature-simulation`, draft PR #25.  
**Main sync prerequisite:** merge `810feba70f01cdc35003ac80ea8486f39dc16049` included `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. Main merge handoff: `docs/agent-handoffs/physis-main-merge-2026-11-18.md`.  
**Owners:** PHYSIS owns climate/nature/field policies; Box3D owns dynamics, NEREID owns canonical water, TerraWeave owns terrain/foliage; new editor code is a host adapter only. No genre dependence.

## Implementation and test SHAs

- `69ffa449b296bb89ef7f6ab35cff255c391a6032`: new `engine/physis/PhysisFieldAssets.h`, typed parser `parseFieldAssets`, stable ASCII authored IDs, no FPS/RTS dependencies, deterministic ordered ECS spawn via `spawnFieldAssets`.
- `8ec562d438f9a393b817c680f53202449d0ef921`: `engine/physis/PhysisProjectConfig.h`: `installFromMap` validates field instances **before installing** PHYSIS, then calls `install` and spawns ordinary WindZone/StormEvent/GravityField components.
- `713f58e6f562698de913729efb5875bd0e02c7a4`: `apps/editor/PhysisAuthoring.h`: editor/native shared validator; `fieldTemplate`, `addField`, `removeField`, `replaceField`, `moveField`, `findField`; MapDoc Undo transaction rules.
- `972528773f00ddfe79bbe8084e9995682756708e`: new `apps/editor/PhysisFieldEditor.h`, PHYSIS field list, typed/JSON properties and opt-in Scene marker mouse hit/drag.
- `bdf204664c189e2fc9cdf81ccd825ed512a12622`: `apps/editor/windows/SceneWindow.cpp`, calls marker input hook before generic FPS/RTS selection; draws authored markers after PHYSIS live diagnostics.
- `0442bcbd7dad33c4f2c53e0e7532fdf064586c84`: `apps/editor/windows/PhysisWindow.cpp`, field list and controls visible even without live PHYSIS simulation.
- `10adfca96470cea19565de8feeb50ca7cfdf9d50`: shared field-properties UI refactoring; selected marker navigates to common MapProps Inspector.
- `f58dd42d05618729a9ec1057f795bc44271a17a4`: `apps/editor/windows/InspectorWindow.cpp`, renders selected field properties through the shared Inspector.
- `e7f12e8069c6ebd4f6c82885cc7ad4d17d35f0ac`: `tests/physis_field_assets_test.cpp`, parser, ID ordering, invalid shapes/flags, world spawn and sample parity source fixtures.
- `7713cb5866fa62ac48bfa19ac5b67b9d85d91ad2`: `tests/physis_field_authoring_test.cpp`, regular/FPS maps, add/replace/remove, duplicate IDs, invalid transactional edits and one-Undo-per-drag source fixtures.
- `d87bc549074a42e3f533de0cd891bbb59577f68a`: `CMakeLists.txt`, CTests `physis_field_assets`, `physis_field_authoring` (the latter conditional on `aiverse_editor_core`).
- `5fb8e83177feb2d7ab101964393ee6c1da82fd22`: focused `.github/workflows/physis.yml` adds native field-assets test target.
- `c4b5e8b8848c40fbed65cb7bd4eb251eff56cc3a`: `.github/workflows/studio-validation.yml` adds authoring/asset tests to editor build.
- `3c2a5a04748ff89ac9019dbbdac6e480bf0f6f28`: extends `tests/physis_project_config_test.cpp` with generic Simulation setup, actual authored field spawning and save/load deterministic continuation.
- `eaa5b27d6a609881af5418b928ef9eb4866abe77`: `docs/physis.md`, user-facing source architecture and JSON example.
- `d80862a9020c2668438d6ff0f2829318e7ea3b74`, `ab3dd1aff1768fe1dac02dba4f48eaaea42d5f93`: root and PHYSIS `AGENTS.md` scope/ownership, flags and Undo rules.
- `2b6afd7cd1420db48c53260e790302874d2710ab`: `skills/world/aiverse-physis/SKILL.md`, AI reuse.
- `3b3b2b051492b2e2ac4f6cd08b9c628ab8898091`: `docs/editor-unification-handoff.md`, editor agent coordination without pretending PR #7 is complete.

## API and behaviour

### Map-side authored assets and generic native runtime

```cpp
#include "engine/physis/PhysisProjectConfig.h"

// Any native GameModule::setup, independent of genres:
const bool installed = aiverse::physis::installFromMap(simulation, map);
```

The previous Stage 18 API read `physis.grid`, `physis.climate` and flags but did not instantiate field arrays. This stage adds typed `windZones`, `storms`, `gravityFields` JSON arrays with:
- Required per-field stable unique ASCII `id` (letters, digits, `_`, `-`, `.`, max 64 characters), `center: [x,y,z]` in Fixed metres (world +Y up) and type-appropriate radius/velocity/dynamics properties.
- WindZone: `radiusMeters`, `velocity` and optional `gustAmplitude`, `seed`, `startTick`, `durationTicks`, `gustPeriodTicks`.
- StormEvent: `kind: "Tornado"|"Hurricane"`, `eyeRadiusMeters`, `outerRadiusMeters` (strictly greater than eye), `heightMeters`, optional `driftPerTick`, wind/updraft/inflow, precipitation, pressure, start/duration, clockwise/enabled.
- GravityField: `mode: "Uniform"|"Attract"|"Repel"`, `radiusMeters`, optional `acceleration`, `radialMagnitude`, nonnegative priority, overrideBaseline/enabled.
- Each nonempty array requires a matching `enableWindZones`, `enableStorms` or `enableGravityFields` config flag so native persistence schemas are stable. All field data is validated **before** `install(sim)`; malformed authored data never partially installs PHYSIS.
- All instances sort by `id` **across categories**, then create ordinary ECS components via `World::create/add`. Map ID does **not** become a persistent ECS schema or runtime entity ID; ordering makes same-map initial population reproducible. Project-owned authoring changes during live simulation do not hot-reload entities. Versioning/migrating saved maps with changed asset IDs/order needs a separate migration policy.

### Editor ownership, selection and Undo

- Under **Мир → PHYSIS · Природа**, create Wind, Tornado/Hurricane or Gravity field at scene camera target, select from list, edit typed properties or advanced JSON, delete with one undo step. The list exists even if current GameModule does not install PHYSIS.
- Enable **Редактировать маркеры мышью**, then click/drag a projected W/S/G marker in the common Scene. Drag adjusts X/Z through the camera ground plane; Shift-drag moves Y. First effective motion validates and opens one `MapDoc::beginEdit` undo snapshot; later movement validates and calls `MapDoc::touched`, giving **one Undo for the complete drag**, not dozens. Marker hit test intercepts mouse only for explicitly opted-in marker interaction; otherwise FPS/RTS Scene picking continues as before.
- Selecting a marker opens shared `Selection::MapProps` Inspector; selected PHYSIS field is a **map asset**, not an invented live ECS identity. The property UI is shared with PHYSIS panel and writes only to document JSON using full validation. No direct mutation of Box3D or live PHYSIS `Simulation::world()`.
- Existing Stage 18 MapProps `physis` section still edits overall PHYSIS grid/climate/flags and supports Undo/Redo. Both normal map `MapData.extra` and FPS verbatim `MapData.fps` are handled.

## Exact validation status

- **PASS (source inspection only):** GitHub connector writes committed. Inspected current source to confirm native loader uses parser before `install`, editor reuses same parser, Scene mouse hook occurs before FPS/RTS selection, Inspector uses shared property drawer, CMake and CI list both new tests; all three new headers had balanced curly braces in naive text inspection. These checks are NOT compilation proof.
- **NOT RUN / UNVERIFIED:** `git clone`/CMake/native C++ build, CTest, GitHub Actions result, real editor window launch, mouse click/drag/Undo operation, visual QA on macOS, serialization runtime continuation. Network isolation previously produced `Could not resolve host: github.com` on local checkout. Do not claim PASS or merge PR #25 based on written test code.

From a native checkout, perform:

```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-editor -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-editor --target aiverse_editor \
  aiverse_physis_project_config_test aiverse_physis_field_assets_test \
  aiverse_physis_field_authoring_test aiverse_physis_authoring_test \
  aiverse_physis_spatial_index_test aiverse_physis_box3d_test -j4
ctest --test-dir build/physis-editor --output-on-failure \
  -R '^physis_(field_assets|field_authoring|project_config|authoring|spatial_index|box3d)$'
```

Then launch the actual editor, enable PHYSIS marker edit mode, add and drag fields (including with Shift), verify selection and parameter update in MapProps, Undo/Redo on both regular and FPS JSON maps, remove/re-add, reopen/roundtrip, move/resize camera, UI themes, no interference with the shared Scene Transform gizmo or normal selection when edit mode is disabled. Inspect actual screen captures and performance.

## Known limits, next stage

1. **Highest priority:** get native compilation/CTest and macOS editor click tests, fix real errors/warnings. Current sources are unverified. At some earlier stages, other agents may have advanced the common editor APIs; inspect main and the editor-unification handoff.
2. Authoring field assets are static map inputs. No live hot-edit/replay-safe field creation/deletion, server-authoritative editor command channel, FieldID ECS lookup or automatic reserialization of modified source IDs. Immutable authored IDs sort initial spawn but are not part of ECS persistence schema.
3. The marker gizmo supports position and Shift-height, typed Inspector supports parameters; it does not yet expose full Unreal-style local axes/rotation/orientation, multi-select, duplicate snap controls or camera-depth-correct 3D hit testing. It must not claim these capabilities.
4. No automatic nature-VFX/Terra/NEREID/Box3D forces beyond previously authored optional adapters. Water NEREID PR #23 owns canonical water; TerraWeave PR #24 owns terrain/foliage. PHYSIS is always genre-neutral.
5. After native verification, performance profile the 2D field index, editor marker counts and overlapping storm fields; add targeted deterministic simulation LOD/streaming if justified.
6. **Every subsequent code stage requires its own handoff** containing implementation SHA, actual PASS/FAIL/NOT RUN, limitations and an actionable next agent step. Preserve PR #25 as draft until tests pass.
