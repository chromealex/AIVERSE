# AIVERSE Universal Niagara-style VFX — agent handoff

**Date:** 2026-11-18
**Working branch:** `feature/universal-entity-inspector-fps`
**Goal:** universal, composable VFX for RTS, FPS, racing, RPG, strategy and native games. Never create an FPS-only or RTS-only particle system. Extend existing `engine/fx/ParticleSystem`; keep VFX presentation-only and authoritative ECS/rollback deterministic.

## Stage 1 — versioned graph frontend (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `5afe37d4ee6418897b49ee5ac3e27cbe32362c40`
**Standalone test source SHA:** `b17ba98f0675be2f628b2177b83d8b210f9250eb`
**CTest registration SHA:** `43f0090c681f5826746a62e51df55fa33c553b50`

### Files

- `engine/fx/VfxGraph.h`: new header-only, version-1 Niagara-like graph authoring IR and compiler.
- `tests/vfx_graph_compiler_test.cpp`: standalone headless compile, branch fan-out, parameter override and invalid-graph checks.
- `CMakeLists.txt`: `aiverse_vfx_graph_tests` / CTest `vfx_graph_compiler` target.

### Architecture and API

```cpp
#include "engine/fx/VfxGraph.h"
aiverse::fx::EffectDef compiled;
std::vector<std::string> errors;
bool ok = aiverse::fx::compileVfxGraph(graphJson, parameterOverrides, compiled, errors);
if (ok) {
    int effect = particleSystem.define("projectile_impact", compiled);
    particleSystem.play(effect, worldPosition);
}
```

Graph data is a JSON object with `version:1`, optional `parameters`, `nodes`, `links`, `outputs`, optional `forceFields`. Nodes: `Spawn`, `Initialize`, zero or more `Update`, and an output `SpriteOutput` / `MeshOutput` / `RibbonOutput`. One path per output: Spawn → Initialize → Update* → Output. Output fan-out is supported; node merges and cycles are rejected. Each node holds `properties` from the existing `FxEffect.h` emitter schema; nested properties merge. Values can refer to a graph parameter using `{"$param":"name"}`, with optional literal overrides. The existing strict `parseEffect` validates resulting emitters.

The resulting `EffectDef` uses the CURRENT CPU simulation and rendering code; it does not execute a graph per particle and does not add compute/GPU particle support. `MeshOutput` requires `meshes`; `RibbonOutput` forces hidden particle ribbon trails.

### Tests and limitations

- **NOT RUN**: no local AIVERSE checkout/build environment was available in this connector-backed stage. `CMakeLists.txt` registration is committed, but neither compilation, CTest, rendering nor CI has been verified. Do not claim the test passes.
- Commands for the next agent:
  ```bash
  cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vfx-test --target aiverse_vfx_graph_tests -j4
  ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_graph_compiler$'
  ```
- **Critical caveat:** Graph v1 is a data IR compiled to the old effect schema, not a complete programmable Niagara graph. No arithmetic/dataflow nodes, custom attributes, multi-input merge, function graphs, runtime GPU kernels, visual graph editor, depth-based collisions or soft particles yet.
- Handle branch concurrency carefully: other agents are changing editor/ECS/Metal code. Never force-push or overwrite unrelated work.
- Existing RTS FX bridge: `modules/rts/fx/RtsFx.*` and `apps/client/ClientFx.*`. Existing particle renderer: `engine/fx/{ParticleSystem,FxEmit,FxRender,FxInternal}.*`; CPU dynamic vertex expansion, batch-by-texture/blend, capped quality tiers.
- Stability: `fx/<name>.json` assets and `fx/fx.json` RTS bindings are unchanged. New graph frontend opt-in; no migration of existing assets.

## Next stages (DO NOT claim completed)

1. **Stage 2:** generic read-only ECS VFX presentation bridge. Add components describing asset + attachment + binding, and a presenter that only *reads* gameplay state and consumes view events, never writes render handles into authoritative World. Test two unrelated genre hosts and entity generations; preserve RTS legacy bridge during migration. Commit a separate stage-2 handoff with actual SHA/tests.
2. **Stage 3:** graph asset file format, loading and caching/hot reload, exposed parameters, reusable typed graph modules, editor preview integration and MCP graph commands; add a dedicated visual graph editor instead of only JSON.
3. **Stage 4:** GPU particle simulation backend (Metal compute first, Vulkan/desktop as supported, fallback to CPU GLES3/WebGL2); data layout, queue/barriers, spawn/update/compact, compute/graphics sync, prefix sum and culling. Do not claim parity on untested backends.
4. **Stage 5:** advanced GPU features: particle collision/depth sampling, scene geometry/physics interface, camera-facing and mesh instancing, sorting, ribbons, lights, GPU event channels, persistent pools, LOD/scalability.
5. **Stage 6:** convert existing RTS/FPS/native integrations to generic VFX contract, cross-genre performance, screenshot and platform CI regression gates. Retire the RTS-only event binding path only after parity.

Every completed stage must follow `AGENTS.md`: implementation commit(s), then a **separate handoff commit in this document** with SHA, paths, actual tests/CI statuses, caveats and next step. Do not promise Unreal source compatibility or a fully working Niagara editor before those stages are implemented.

## Stage 2 — cross-genre ECS presentation system (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `60657dded40c58bd07b701df7c7ea1bd34b94120`
**Standalone test SHA:** `7c743f48a6c92112a9756392d1965bf964be1620`
**CTest registration SHA:** `e19a0ac9074bd3c4aa714396e748f68eedb95b63`

### Files

- `libraries/include/aiverselib/genre/EcsVfxPresentation.h`: data-only `VfxSourceComponent`, `VfxEventBinding` and read-only `VfxPresentationSystem`.
- `tests/vfx_ecs_presentation_test.cpp`: two distinct ECS component types (space unit, racing vehicle), emitter attach/move, event playback, generation reuse, reset.
- `CMakeLists.txt`: `aiverse_vfx_ecs_tests` and CTest `vfx_ecs_presentation`.

### Public integration

```cpp
#include <aiverselib/genre/EcsVfxPresentation.h>
using namespace aiverse::lib::genre;
auto& source = world.add<VfxSourceComponent>(entity);
source.effect = "engine_fire";  // existing fx asset or compiled graph output
VfxPresentationSystem presenter(particles);
presenter.sync(world, [](const World& w, Entity e,
                         aiverse::fx::float3& pos, aiverse::fx::float3& dir) {
    // Resolve render transform from an arbitrary native project component.
    return resolveYourPresentationPose(w, e, pos, dir);
});
// After each committed tick, not after each render frame:
presenter.consumeTickEvents(tick, sim.events(), {{hitEventId, {"impact_flash", 1_fx}}});
// On new match, rollback/resimulation or preview reset:
presenter.reset();
```

`VfxSourceComponent` has effect name, fixed-point scale, revision, enabled flag, and kill/fade on detach. Generation-packed Entity keys prevent handles leaking to recycled entities. If an attachment is out of view / budget rejected, the bridge retries when it becomes visible. Do not attach one-shot assets through this component (the bridge will restart non-looped effects); use `consumeTickEvents` for one-shots. The game-specific host provides pose resolution and the mapping from module-defined `SimEvent.type` IDs to effect assets; this package contains **no RTS/FPS includes** and writes **nothing** to authoritative ECS.

### Verification

**NOT RUN:** sources and standalone test target committed, but no C++ compilation, CTest, CI or screenshot tests have completed in this environment. Required commands:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation)$'
```

### Constraints / next step

- Neither RTS nor FPS game loops have been migrated to call this adapter yet. The old `RtsFx` bindings must remain until event parity is verified; do not claim integration done.
- The bridge consumes `SimEvent` and `World` **only** on the presentation side, not via an ECS gameplay `Simulation::addSystem`; timing remains under the host's render loop.
- Duplicate-tick suppression is global per presenter. On rollback or replay, call `reset()` before replay; split-screen/multiple worlds need separate presenters. Future work should generalize stable presentation event IDs across snapshot/resimulation.
- Next: Stage 3 — load and hot-reload versioned graph assets in a reusable registry; integrate graph asset edits with the existing FX editor/MCP and add graph-aware tests. Then GPU simulation and visual node editor are separate stages, not implicit in the current work.

## Stage 3 — graph assets, exposed-parameter variants and hot reload (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `582763839d818e04287723cba6143453648680d0`
**Standalone test SHA:** `26a3acb7f2d6511ef9a070539e56ed7528ca1214`
**CTest registration SHA:** `cd5f25b784a1beab1af2c5177376e239338fcf57`

### Files

- `engine/fx/VfxGraphAssets.h`: public header-only `VfxGraphAssets` and `VfxGraphLoadResult` (CPU graph compilation registry).
- `tests/vfx_graph_assets_test.cpp`: graph load, unchanged-content cache, parameter variants, reload after source change, last-good fallback on malformed JSON, path traversal rejection and invalidate.
- `CMakeLists.txt`: `aiverse_vfx_assets_tests`, CTest `vfx_graph_assets`.

### Stable public C++ API

```cpp
#include "engine/fx/VfxGraphAssets.h"
aiverse::fx::VfxGraphAssets assets(particles,
    [](const std::string& path, std::string& text) {
        return readGameContent(path, text); // provided by the native game
    });
auto result = assets.load("engine_fire"); // fx/graphs/engine_fire.vfx.json
auto variant = assets.load("engine_fire", aiverse::Json::parse(R"({"size":2})"));
if (result.effect >= 0) {
    particles.play(result.effect, {0,0,0});
    // A VfxSourceComponent may reference result.runtimeName once loaded.
}
// The next assets.load() checks the source content hash and recompiles on changes.
// Invalid reloads return the previous good compiled effect plus errors.
```

The registry hashes source content using stable FNV-1a 64-bit and builds parameter variant names from stable serialized override objects. Graph output materializes into an existing `ParticleSystem::define` effect, with separate effect identities for each exposed-parameter variant. `fx/graphs/<name>.vfx.json` names are validated (A-Z/a-z/0-9/_/-). The renderer's CPU effect ID is retained for an in-place valid hot reload. Failed reloads do not overwrite a working effect. `invalidate()` forces recompilation on the next load. The registry intentionally does not perform filesystem IO itself; native games can use archives or platform readers.

**Compatibility:** old `fx/<name>.json` and old `fx/fx.json` remain unchanged. Native games can opt in through public headers; no render-server or gameplay simulation dependency was added.

**NOT RUN:** no local compiler, CTest, CI status, Metal device or screenshot verification was available in this execution context. Required compile/run commands:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation|graph_assets)$'
```

### Limitations and immediate next action

- Graph loading is **not yet wired** into the existing FX editor or agent MCP commands; no visual node editor, shader/compute nodes or GPU simulation exists. The runtime registry is callable by native projects today once compiled, but the existing RTS and FPS production game loops do not consume it automatically.
- A fallback on bad asset reload keeps the previous in-memory effect; an explicit `ParticleSystem::reloadAll()` invalidates stored effect IDs, so the host must also clear/invalidate its `VfxGraphAssets` registry before reloading. A future integrated hot-reload service should make this automatic.
- Next bounded stage: editor/MCP integration of graph resources, graph node canvas, parameter editing and graph-to-effect preview; **then** introduce GPU compute backend with CPU fallback and platform-specific capability checks.
- After every next implementation stage, append exact implementation SHA, branch, paths, actual tests and handoff commit here; do not mark stage validated until the test commands pass.

## Stage 2 corrective checkpoint — rollback one-shot cleanup

**Implementation SHA:** `9cc82382781e852f307b7cf7ca083aff36ad07a4`
**File:** `libraries/include/aiverselib/genre/EcsVfxPresentation.h`
The presenter now retains up to 65,536 recent event-spawned `FxHandle` values and kills those handles during `reset()`; this prevents recent one-shot remnants being left in the scene after rollback. Beyond this bounded history, very old handles cannot be retracted by the presenter: the cap keeps bookkeeping bounded. For effects older than the cap that must disappear, the host may reset its entire particle world at new-match boundaries.

**Tests:** NOT RUN. The stage-2 test currently exercises event playback and reset cursor, but does not assert old one-shot particle removal; extend `tests/vfx_ecs_presentation_test.cpp` before declaring rollback fully verified. The `FxHandle` cleanup also needs a high-volume profiling pass.

**Next:** finish stage-3 editor/MCP integration or introduce the graph GPU backend only after compiling all three new tests and verifying platform support.

## Stage 4 — AI skill, AGENTS rule and discovery registration (COMMITTED, TESTS NOT RUN)

**Skill SHA:** `040a283087f3d28a2939d260909dccdc480a6292`
**AGENTS.md SHA:** `14230d7c1e7dbbfb5c4f8f0e70bdcac252faad76`
**Skill generator SHA:** `b054038ef1bd1c51b02a1a74cc5245a204d9cdf8`
**Generated catalog SHA:** `d48037587ec193a2e46133a8e629caa641d0df50`

Files: `skills/visuals/aiverse-niagara-vfx/SKILL.md` (new), `AGENTS.md`, `tools/skills/catalog.py`, `skills/catalog.json`. AI agents now have a durable skill with versioned graph JSON example, current CPU backend and ECS API, cross-genre integration rules, outstanding tasks and reproducible test commands. AGENTS.md links this handoff and forbids genre forks or false claims that the GPU/editor portion is done. The skill generator and catalog both list `aiverse-niagara-vfx` with explicit search aliases.

**Tests NOT RUN.** Run `python3 tools/skills/catalog.py check` as well as the three new VFX CTest targets. The catalog JSON skill list is in lexical ID order, but this is not equivalent to a generator validation. As with all stages, code presence does not prove build correctness.

**Next stage:** implement editor/MCP graph asset CRUD and a visual node canvas; validate tool bindings and cross-project native import. GPU backend needs a separate device capability/RHI design, shader compilation and performance testing before enabling in production. Do not modify authoritative ECS or replace legacy FX until equivalent presentation behavior is measured.

## Stage 5 — bounded O(1) event handle tracking and rollback test (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `d1766010fffc405578c2b41c5d0e4e812fb9b3e0`
**Test patch SHA:** `ce19be88d787ee26ce851bf1fc43d99b4734de27`

- `libraries/include/aiverselib/genre/EcsVfxPresentation.h`: replaced `erase(begin)` in recent one-shot handle history with a bounded ring overwrite; each event capture is now O(1) even after the 65,536-handle limit. Rollback reset still kills all handles retained in the ring.
- `tests/vfx_ecs_presentation_test.cpp`: added an assertion that `reset()` clears both attached particles and recent event-spawned one-shots from the headless CPU particle snapshot.
- **TESTS NOT RUN.** The updated source and test were committed, not compiled or executed. Run `ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation|graph_assets)$'` after building targets.
- **Risk:** Very old one-shot effects whose handles have been overwritten by the bounded ring cannot be killed by a presenter-only reset. Their natural expiration is expected for normal short-lived effects. A production full-world reset can instead clear all particles, if the host owns the entire particle world.
- **Next:** integrate graph assets into the editor and MCP, then implement an actual graph canvas and GPU backend; user-facing Niagara parity is NOT delivered by these foundational stages.

## Stage 6 — official product identity: AIVERSE SpectraFX (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**User request:** invent a distinctive VFX-system name, actually use it in the engine's code and leave a durable handoff. **Decision: AIVERSE SpectraFX** (public namespace `aiverse::spectra`; skill `aiverse-spectrafx`). Unreal Niagara remains an inspiration/reference, **not** the product name.

### Commits and files (in implementation order)

| Commit | File | Change |
|---|---|---|
| `b2e684492661b14d79efba33276659d10a4a5d67` | `engine/fx/VfxGraph.h` | Real `compileSpectraGraph` functions, compatibility `compileVfxGraph` wrappers |
| `6c1d503dcbd23359b04703f280c47e0f7d7aaf5e` | `engine/fx/VfxGraphAssets.h` | Canonical `SpectraGraphAssets`/`SpectraGraphLoadResult` with old `Vfx*` type aliases |
| `0ee7a5a2dad956e38b63d67d94cfc2a7f043f427` | `libraries/include/aiverselib/genre/EcsVfxPresentation.h` | Canonical `SpectraSourceComponent`, `SpectraEventBinding`, `SpectraPresentationSystem`; aliases preserve component identity |
| `92eb8d3375b8963d6a7696e31d8c72810028796d` | `engine/fx/SpectraFX.h` | Public `aiverse::spectra::{Runtime,Effect,Handle,Quality,GraphAssets,GraphLoadResult,compileGraph,kName}` |
| `7cfca2c6e3a7f430a86f6bb20a6037a7459ed119` | `libraries/include/aiverselib/genre/SpectraFX.h` | Native-project `aiverse::spectra::{SourceComponent,EventBinding,PresentationSystem}` |
| `78e57322ab1e668bc585e3ef31c9ac64911a81e0` | `tests/vfx_graph_compiler_test.cpp` | Uses branded public `spectra::compileGraph` |
| `b88671bdd26c7170839b162e00e96186ac92f148` | `tests/vfx_graph_assets_test.cpp` | Uses `spectra::Runtime`/`spectra::GraphAssets` |
| `7e2e47c277978d8f50beca1775f8b9b8251c4d67` | `tests/vfx_ecs_presentation_test.cpp` | Exercises `spectra::SourceComponent`/`PresentationSystem` |
| `e3b4d7e0e6b78f05f2f51016b24981d1d1c17294` | `CMakeLists.txt` | Adds `AIVERSE::spectrafx` alias for `aiverse_fx` and labels regression tests `SpectraFX` |
| `770fa8ec42dc4a9669f16e8d2d88eafb31215444` | `docs/spectrafx.md` | Official architecture/API/graph schema, integration and roadmap |
| `44918a3d9de7321645a67808c7db7a2ece938dec` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Canonical AI skill |
| `a3b3b590a659527338382a76675307cc37483a73` | `skills/visuals/aiverse-niagara-vfx/SKILL.md` | Legacy skill redirects to official branding |
| `1cab6922a806d305fbe88e9a5fafe7dcb7490fb9` | `tools/skills/catalog.py` | Adds official `aiverse-spectrafx` workflow/search and keeps Niagara alias |
| `bb734cb476f1732751fb6a1003082c5720e93700` | `skills/catalog.json` | New canonical skill entry + legacy entry updated |
| `a327028b6e9350601b25951836995fc3b394ff45` | `docs/particles.md` | Explains SpectraFX overlay on existing CPU effects, incomplete future work |
| `6f19737e427ee6281fe482b5d00139144d398d53` | `AGENTS.md` | Official name, headers, CMake target, skill, handoff contract |

### New canonical native API

```cpp
#include <aiverselib/genre/SpectraFX.h>
#include "engine/fx/SpectraFX.h"

aiverse::spectra::Runtime particles;
aiverse::spectra::GraphAssets graphAssets(particles, readContent);
auto compiled = graphAssets.load("rocket_exhaust");
if (compiled.effect >= 0)
    particles.play(compiled.effect, {0, 1, 0});

aiverse::spectra::SourceComponent component;
component.effect = compiled.runtimeName;
world.add<aiverse::spectra::SourceComponent>(entity, component);

aiverse::spectra::PresentationSystem presenter(particles);
// presenter.sync(world, nativePoseResolver);
// presenter.consumeTickEvents(tick, sim.events(), gameEventBindings);
```

Native CMake: `target_link_libraries(my_native_target PRIVATE AIVERSE::spectrafx)` (include dirs already follow the existing aiverse_fx target; `libraries/include` must also be in the native project's include paths for the `aiverselib/genre` header).

**Migration/compatibility:** no change to legacy `fx/<name>.json`, `fx/fx.json`, old effect runtime IDs, serialized simulation state or `RtsFx`; no duplicate renderer. Old names `compileVfxGraph`, `VfxGraphAssets`, `VfxSourceComponent`, `VfxPresentationSystem`, `VfxEventBinding` are source-level aliases/wrappers. Because the ECS component type aliases resolve to the exact same types, existing ECS type IDs are preserved (do not replace aliases with wrappers or derived types). Graph asset path stays `fx/graphs/*.vfx.json` for compatibility.

### Test record — NOT RUN

No local checkout/build/CTest runner was available while making the connector-backed edits, so compilation, `ctest`, skill catalog check and visual QA have **NOT** been executed; do not claim success. Next agent must run:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

### Known limitations and exact next task

1. Confirm that the new renamed C++ types and aliases compile/link, that all three tests pass, and that generated `skills/catalog.json` equals the catalog generator's output. Fix immediately if not; report actual outputs.
2. Hook `SpectraFX` graph asset CRUD, preview and runtime loading into `apps/editor/core/FxOps.*` / `Workspace` / the existing Effects window and MCP registration, without breaking legacy effect operations; then create a visual graph editor.
3. Add GPU compute particle backend with explicit RHI capability checks and CPU fallback; never call existing graph v1 a full Niagara dataflow/GPU implementation.
4. Validate genre-neutral actual host usage for FPS and RTS (not only header-level fixtures), entity attachment, effect IDs, rollback and visuals, then migration/parity.
5. Continue appending handoff entries **in this same file**, separately committed after each implementation stage; preserve all other agents' changes to this active branch.

## Stage 7 — SpectraFX MCP graph authoring and CPU preview bake (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** make SpectraFX graph-v1 frontend available through both editor Workspace and headless MCP without genre-specific simulation dependencies.

### Implementation and documentation commits

| SHA | File | Change |
|---|---|---|
| `cceac6c08471785a19b3556f474ad276616c23f3` | `apps/editor/core/SpectraGraphOps.cpp` | Six universal `fx_graph_*` operations |
| `b71ad7b6dd00c4b44b09312b430c272089011968` | `apps/editor/core/FxOps.cpp` | Register graph operations with existing FX tools |
| `13ffbf60776c71681603bde2381c9b1ddc40b3f5` | `tests/vfx_graph_ops_test.cpp` | Workspace graph CRUD, validation, bake, safety regression |
| `a8609363e7eed3ee7d53b64af4d2884a8d350f65` | `CMakeLists.txt` | Build and CTest target `aiverse_vfx_ops_tests` / `vfx_graph_ops` |
| `510bd7e11fbf6c8269334f94c7abc3cd04245f35` | `docs/spectrafx.md` | Document MCP graph API |
| `4d7856a46d4e47130d7cb56718bb3227bffeeb4b` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent graph-edit workflow |

### API, architecture and compatibility

Registered in `registerFxOps` through `registerSpectraGraphOps`: `fx_graph_list`, `fx_graph_get`, `fx_graph_validate`, `fx_graph_set`, `fx_graph_delete`, `fx_graph_bake`. Graph validation uses `aiverse::spectra::compileGraph` and is CPU-only. Graphs live in `fx/graphs/<name>.vfx.json` and are limited to 1 MiB. `fx_graph_set` rejects invalid compilation without overwriting the source; valid changes call `Workspace::note(Change::Fx)`. `fx_graph_bake {name,effect,overrides?,overwrite?}` materializes a separate `fx/<effect>.json` compatible with existing `fx_preview` and legacy clients; old effect files are not overwritten unless `overwrite:true`. Bake is a snapshot: editing source graphs does not automatically refresh baked effects.

Game simulation, deterministic ECS authority, rollback state and old `RtsFx` are untouched. There is no node-canvas editor and no GPU simulation in this stage.

### Verification status — NOT RUN

GitHub connector files and commits were created. No local AIVERSE checkout, build, CTest result or screenshot was available in this stage. **No test is claimed passing.** New `vfx_graph_ops` test was added but has not executed.

Commands for the next agent:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Review the new `SpectraGraphOps.cpp` and `vfx_graph_ops_test.cpp` for actual build/runtime errors before production usage. This must be done independently of GitHub source presence.

**Next bounded stage:** editor node-canvas for the exact graph-v1 schema (select/add/move/link/delete nodes, exposed parameters, compile diagnostics, preview through `fx_graph_bake`/existing CPU renderer); then standalone GPU RHI capability and compute-stage design. After each implementation stage append a **separate handoff commit** to this same file, listing SHAs, tests actually executed and the next action.

## Stage 8 — visual SpectraFX node canvas and in-memory preview (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Scope:** Basic authoring/preview of the existing graph-v1 format for **all genres**; no additional gameplay authority, no forked FPS/RTS renderer and no GPU compute claims.

### Implementation commits / paths

| SHA | Path | Change |
|---|---|---|
| `87e2c5647d1502514807664c1785e8858c51ce16` | `engine/fx/VfxGraph.h` | Optional `nodes[].position:[x,y]` authoring metadata, validated and ignored by CPU compiler |
| `458d2794a09fda86f8d39494ee393b82fcca6638` | `apps/editor/windows/SpectraGraphWindow.cpp` | Optional ImGui SpectraFX window: load/new/save, graph canvas, panning, add/drag/link/delete, inspector JSON, parameters, undo/redo, compile messages |
| `a58becfbde9d1bdd03c04d30aa10a16f2515617e` | `apps/editor/EditorFxOps.cpp` | GUI-only `fx_graph_preview`: compile graph in memory into `gameFx_` and play in Scene/Game |
| `5f08de8ed7c931e4bff460973fe71362eb88ad51` | `apps/editor/EditorFxOps.cpp` | Correct escaped JSON screenshot hint and newlines |
| `95357caa81f68bd6cd4709a783e97dc82daf0d20` | `apps/editor/windows/SpectraGraphWindow.cpp` | Include `FxOps.h` for canonical graph asset file-name validation |
| `868c077aa9288f9de77d624944525308d48f3bb9` | `tests/vfx_graph_compiler_test.cpp` | Authoring metadata accepts valid positions, rejects malformed/out-of-range positions, has no effect on CPU emitter output |
| `3aa17d390fae819458cc3f89127f25804e3b2877` | `engine/fx/VfxGraphAssets.h` | Include `ParticleSystem.h` explicitly so SpectraFX public headers are self-contained |
| `ed968dda74724abbad926709944dd6a6a278db24` | `docs/spectrafx.md` | Basic canvas, graph preview, saved node metadata, limitations |
| `08fda8e982eb494a089e02d8a49336b04453a397` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent GUI workflow and preview tool |
| `2acb0cd9d22b30c6136d8dfafa7a71a2615fc0a5` | `docs/particles.md` | Update capability/limitation status |
| `b191861b09eca4050f41c820276be4ee977cbf3f` | `AGENTS.md` | Official universal VFX canvas/preview integration rules |
| `594e4793fbe079bdc2340f2be7aa992eca8fc48e` | `engine/fx/SpectraFX.h` | Update public header capability comment |

### API, authoring data and compatibility

- The optional editor window is registered as `id()=="spectrafx_graph"`, title **SpectraFX**, window group `look`. Existing CMake `file(GLOB ... apps/editor/windows/*.cpp)` picks it up without a CMake registration edit.
- Graph nodes retain existing `{id,type,properties}` and may optionally add `"position":[x,y]` in **the same** `fx/graphs/<name>.vfx.json`. Compiler validates two numeric coordinates, absolute bound <=100000 and then ignores them when constructing `EffectDef`. Older graph sources remain compatible, with computed initial visual layout. This is source-file-only metadata and does **not** affect simulation save formats, ECS checksums or rollback.
- Basic canvas: click a node header to select; left-drag to move; middle-drag pans; click the output pin and then another node's input to connect (single parent / cycle guard); add existing v1 node types through combo; edit the selected node's `properties` as JSON; edit root `parameters` JSON in a modal; undo/redo the full document.
- Save invokes `Workspace::call("fx_graph_set",...)` (strict validation, `Change::Fx`). Invalid/incomplete graph edits can be drafted but not saved as valid assets. Existing `fx/*.json` unaffected.
- New GUI-only `fx_graph_preview {graph, name?, overrides?, x?,y?,z?, advance?,loop?,freeze?}` compiles the **unsaved** graph and calls `ParticleSystem::define("__spectrafx_editor_preview",...)` then `play` with the Scene/Game camera. Unlike `fx_graph_bake`, this command writes no FX files; `view_screenshot` remains the visual verification tool.
- Currently the property inspector edits raw per-node JSON rather than typed Niagara-style individual module pin values. The graph is an ordered module-overlay IR, not an arbitrary runnable dataflow graph.
- `SpectraGraphAssets` now explicitly includes `ParticleSystem.h` for correct standalone header inclusion; no public symbol changed.

### Verification record — NOT RUN

GitHub file creation and per-file commits succeeded. **No CMake build, CTest execution, editor launch, screenshot, Metal/Android validation or runtime FPS/RTS parity test was performed.** This environment has no local AIVERSE checkout, and source review is **not** proof of successful compilation.

The next local agent MUST execute:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then launch AIVERSE Editor, open **Графика → SpectraFX**, verify create/save/load, node drag/link/relink/cycle prevention, undo/redo, JSON property edits, legacy file preservation, preview and `view_screenshot`. Also test opening an older graph without `position` and re-saving it.

### Risks and exact next stage

1. **Build/UX unverified:** Inspect `SpectraGraphWindow.cpp` and `EditorFxOps.cpp` for compiler/platform errors and ImGui interactions; fix after a real build. Test the editor only when its existing unrelated branch gates are green.
2. **Graph behavior:** Raw property JSON and graph-v1 single-parent paths are an initial authoring UI only. Empty/disconnected output graphs fail validation by design; add a typed parameter/module inspector and UX for incomplete graph drafts, plus limits on high-density canvas interactions.
3. **Preview lifecycle:** The ephemeral effect uses a reserved runtime name inside the editor's existing CPU particle system. Reloading the particle registry from another window invalidates this preview; add a dedicated visual preview resource or reload-aware integration, and validate multi-window interactions.
4. **Engine GPU work still absent:** Design explicit RHI compute capability checks and GPU particle buffers (spawn/update/compact, emitters, alive counters, sorting, events), CPU fallback for GLES3/WebGL2 and per-device benchmarks; do not silently enable GPU path before parity tests.
5. **Next bounded implementation:** First run/repair native builds and vfx tests, then extend SpectraFX v1 module editor and production genre-neutral rendering adapters. Record the next implementation SHA/test outcomes in a separate handoff commit **before** beginning another stage.

## Stage 9 — typed SpectraFX module inspector and source preservation (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** replace blind raw-JSON edits for common graph-v1 fields with shared, validated field metadata, without losing unrecognized/complex emitter properties or binding logic to one game genre.

### Exact committed changes

| Commit SHA | File | Change |
|---|---|---|
| `fef79683d36a7bf1d707bf2023f1700eccb7359a` | `engine/fx/SpectraModuleSchema.h` | New pure C++ authoring field specifications and safe typed field validation/update/remove |
| `ecce17c209acd851f9051ad0fe9ae940900e3869` | `apps/editor/windows/SpectraGraphWindow.cpp` | Generic typed UI controls in SpectraFX graph-v1 inspector with advanced JSON fallback |
| `c8baa6bf043984dbcd4ef4d2a54a1dedc0b025e6` | `tests/vfx_graph_compiler_test.cpp` | Added schema/validation/no-mutation/unknown-property retention/CPU-compile checks |
| `de2d28d41348c9d5865d0c080223955dbad7ff06` | `engine/fx/SpectraFX.h` | Exposes schema through official public native C++ header |
| `d2d5f69bd192fe537e5f77292cd34b080ea1596c` | `docs/spectrafx.md` | Typed authoring contract, supported common field list and limitations |
| `c97c5272b8fbf4d8414c3f0410f7c29d7eadbc8d` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent workflow for typed fields and complex module fallback |

### Architecture, APIs, serialization

The header `engine/fx/SpectraModuleSchema.h` defines `aiverse::spectra::FieldKind`, `FieldSpec`, `fieldsForNode(type)`, `fieldForNode(type,key)`, `validateTypedField(spec,value,error)`, `applyTypedField(node,key,value,error)`, `removeTypedField(node,key,error)`. The canonical include `engine/fx/SpectraFX.h` exports these declarations. This metadata is pure authoring logic and is **not** a replacement for the existing strict emitter loader `fx::parseEffect`.

Typed fields cover common `Spawn`, `Initialize`, `Update`, `SpriteOutput`, `MeshOutput` and `RibbonOutput` properties. Widgets support bounded number/integer, boolean, nonempty string, validated enum choice and finite 3D vectors. An edit is performed on a copied node and committed only if its own field validation succeeds. Updating or removing a typed field leaves unknown and unrelated JSON properties untouched. The GUI shows complex existing values (curves/ranges) as JSON-only rather than converting them to scalar values. Selected node's raw JSON inspector stays available for all advanced/unsupported data.

The graph format is unchanged (version 1, `nodes[].properties`, `nodes[].position`), so legacy `fx/*.json`, graph assets, old C++ aliases and simulation save files remain compatible. Nothing writes renderer handles to deterministic ECS state, and no GPU compute code was added. Runtime authoring, CPU effect, ECS event and client modules were not modified.

### Tests actually executed

**NOT RUN:** There is no local AIVERSE checkout in this environment and external GitHub access from the container was unavailable (DNS failure). GitHub connector writes and reads succeeded, but do not establish compiler or runtime correctness. `tests/vfx_graph_compiler_test.cpp` was extended; neither CMake/CTest nor GUI interaction was run. No CI status was verified. Therefore **do not mark stage 9 production-ready**.

Local QA commands for the next agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Manual editor QA: open **Графика → SpectraFX**; test each typed control, non-destructive curve/gradient handling, undo/redo for sliders and text fields, JSON inspector synchronization, project switching, save/reload, bad enum handling and Scene preview. Check native includes and CMake public target `AIVERSE::spectrafx` compile on macOS.

### Risks, known limitations and exact next stage

1. GUI code has not been compiled or interacted with on a real ImGui renderer. In particular, validate active-widget undo grouping, String text editing, default field additions and multi-window preview lifecycle. Fix actual build/UI failures before calling this complete.
2. The schema covers only frequent scalar/vector fields, not a full Niagara module library. Nested `noise`, `collision`, `trail`, `light`, `subEmitters`, curves, gradients and complex shader data still require the existing JSON/particle tools.
3. The typed schema is intentionally a convenience layer; `parseEffect` is the final authority when saving an entire graph. Typed edits can leave an incomplete graph until connected.
4. No GPU backend or GPU execution is present. The next independent stage should **first verify and repair stage 7–9 builds/tests**, then design and implement RHI compute capabilities, GPU simulation buffers and a CPU/GPU execution selector with GLES3/WebGL fallback. Do not merge genre-specific VFX engines, alter deterministic ECS ownership, or make unverified performance/parity claims.
5. After the next bounded code stage, append another **separate handoff commit** to this file with implementation SHA, actual test results, risks and precise continuation steps. Preserve all concurrent agents' work.

## Stage 10 — Metal GPU compute particle subset with CPU fallback (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** take the first bounded step from CPU-only SpectraFX to a GPU compute particle backend using the existing RHI, preserving CPU compatibility and all RTS/FPS gameplay/event behavior.

### Verified source findings before design

- The RHI `engine/gfx/Gfx.h` already defines an optional compute contract: `supportsCompute`, `createCompute`, `beginCompute`, `setComputeBuffer`, `setComputeUniforms`, `dispatch`, `endCompute`, storage buffers and debug readback.
- Current `engine/gfx/metal/GfxMetal.mm` implements compute and guarantees per-dispatch barriers; storage `MTLBuffer` can be bound through `setVertexBuffer` in the render encoder. The existing `engine/cloth/ClothGpu.cpp` uses this API.
- Vulkan, GL33 and GLES3 in this branch do **not** expose a functional general RHI compute implementation. Do not activate the GPU path there on the basis of GLSL source alone.
- Existing CPU emitter contract is in `engine/fx/ParticleSystem.*`; `modules/rts/fx/RtsFx.*` remains production.

### Implementation commits

| SHA | File | Change |
|---|---|---|
| `c0e79b7b77c61898f34512cae431ac65c8deacc3` | `engine/fx/SpectraGpuShaders.h` | Metal compute kernel for spawn, integration, GPU billboard expansion; matching GLSL 430 source held for future Vulkan backend (NOT USED) |
| `397cd205d7a84e9555d96cb9d4c68188807d0395` | `engine/fx/SpectraGpuParticles.h` | Public opt-in CPU/Metal selection, config, stats, lifecycle and emitter API |
| `4f1c84f436979130bbc187d4df5f1630d2542a3e` | `engine/fx/SpectraGpuParticles.cpp` | GPU buffer allocation, compute queue, additive GPU render, CPU `fx::ParticleSystem` fallback |
| `c9cfd28ad1d98dc2c17a646332d7eb02176eed3f` | `tests/vfx_gpu_backend_test.cpp` | Pure backend-selection and headless CPU fallback regression |
| `a67b0194c9b4bd5c1c53bc661ede89bf92b9fd4b` | `CMakeLists.txt` | Link new .cpp into `aiverse_fx`, register CTest `vfx_gpu_backend` with SpectraFX label |
| `c340bbf90479744b71940263a14183d2e2ac3a04` | `engine/fx/SpectraFX.h` | Expose the optional GPU emitter through the existing public native header |
| `c791f75567aae2e3456163272d990164e062cac4` | `tests/vfx_gpu_backend_test.cpp` | Add recording Metal RHI mock checking dispatch order and draw command contract |
| `1a0521ba3a4df37f58bd49ff614becb35889541c` | `docs/spectrafx.md` | Native usage, supported subset, RHI requirements and explicit limitations |
| `6f595a58643c95e57248e4945837797d3e9e7517` | `docs/spectrafx.md` | Document mock RHI coverage without claiming actual GPU execution |
| `2714f57d6be439f88aba36e5c19194ec18eb6461` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent instructions for GPU prototype, fallback, invocation and testing |
| `368a5522c2d42d957d1a3ca6c11fe90bfc45e18f` | `AGENTS.md` | Preserve shared ECS/CPU contract and warn against unverified production GPU enablement |

### Public API, layout and platform behavior

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::ComputeParticles emitter;
aiverse::spectra::ComputeEmitterDesc cfg; // capacity 1..4096, burst 1..64
cfg.backend = aiverse::spectra::ComputePreference::Automatic;
cfg.lifetime = 0.8f;
cfg.speed = 3.0f;
cfg.size = 0.35f;
cfg.gravityY = -9.81f;
if (!emitter.init(device, cfg)) { /* emitter.reason() */ }
// Per presentation frame: AFTER Device::beginFrame and BEFORE render pass:
emitter.setView(camera, viewProj, width, height);
emitter.burstAt({0,2,0}, {0,1,0}, {1,0.4f,0.1f,1});
if (!emitter.step(1.f / 60)) { /* timing / resource error */ }
// INSIDE open render pass:
emitter.draw();
// Shutdown before gfx::Device destruction.
emitter.shutdown();
```

- `ComputePreference`: `Automatic`, `CpuOnly`, `PreferGpu`, `RequireGpu`. `RequireGpu` returns an error on unsupported GPU or shader initialization failure; `Automatic` and `PreferGpu` fall back to the original CPU ParticleSystem. Current GPU choice is Metal only when `Device::supportsCompute()` and quality is at least Medium.
- `ComputeBackend`: `Cpu`, `Metal`; pure `chooseComputeBackend` available for native use/tests. Device pointer may be null for CPU-only headless tests.
- `ComputeEmitterDesc` supports the **narrow** additive billboard effect subset: bounded capacity/burst count, lifetime, speed, size, Y gravity, drag, quality, backend policy. `burstAt` supplies world-space position/direction/tint; this is not general graph-v1 automatic GPU execution.
- Persistent Metal storage buffers: 64-byte particle `positionAge/velocityLife/tint/size` state, 64-byte seed records, uint32 spawn slots, and 40-byte expanded GPU vertex per quad corner; static 16-bit indices and a soft RGBA8 texture. The renderer reuses existing vertex/fragment particle shaders and RHI draw/pass bindings.
- Each `step`: enqueue CPU-written seed+slot arrays as Metal blits, dispatch compute mode 0 (birth), mode 1 (age, speed, gravity/drag, position), mode 2 (expand billboard geometry), then draw without GPU→CPU readback. A CPU-side time-to-live/free-slot pool is bookkeeping only (particle motion is computed on Metal).
- GPU render currently draws the full allocated slot index array; inactive slots emit degenerate zero-alpha quads. There is **NO GPU compaction, indirect count or per-particle occlusion/culling/sorting**. Maximum 4096 slots prevents unbounded GPU work.
- All existing `fx/*.json`, `fx/graphs/*.vfx.json`, `RtsFx`, native API aliases and ECS save/rollback formats are unchanged. There is **no production RTS/FPS integration** in this stage.

### Actual verification (DO NOT OVERSTATE)

**NOT RUN:** No local AIVERSE checkout/compiler was available from the GitHub connector environment. GitHub commits succeeded, but neither native CMake build nor CTest nor Apple Metal shader compilation/Apple Silicon frame output was performed. A recording RHI mock was authored, not executed; it does not stand in for a real GPU test.

CI checked on commit `a67b0194c9b4bd5c1c53bc661ede89bf92b9fd4b`: GPU batching regression, ECS experimental checks and Studio validation were **queued**, the general CI run was **cancelled**, and the commit status endpoint contained no success statuses. No passing CI claim is authorized.

Local commands for next agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then run a **real Metal** offscreen smoke: execute `step()` in a frame *before* `beginPass`, inspect MSL compilation output, render several consecutive bursts in one scene, capture frames and compare with CPU for correct birth/death, alpha fade, position and depth. Run the same native example with `CpuOnly` and on GLES3/Vulkan fallback. Stress repeated `clear`, resize/reinit, GPU allocation failure, GPU encoder failure, multiple frames in flight, quality tiers, and owner thread.

### Limitations and exact next stage

1. **Compilation/Metal output unverified.** Resolve real build errors, RHI shader errors and cross-frame buffer hazards before enabling the GPU path in any production project. The mock verifies only host API ordering if it compiles/runs; no GPU execution.
2. **No GPU graph execution.** `CompileParticles` (correct spelling: **`ComputeParticles`**) is a manual opt-in simple billboard emitter. Complex FX remain the CPU backend. Do not claim Niagara parity or convert graph-v1 assets to GPU automatically.
3. **Single pool and material.** Implement a reusable emitter scheduler, per-emitter storage ranges, batching by material, dead-particle compaction, scalable indirect draw, alpha sort and transparent rendering before targeting large particle counts.
4. **CPU-only backends.** Vulkan compute is not wired through the current RHI; GLSL 430 kernel text is only a placeholder. Do not pretend it runs under GL33/GLES3/WebGL2.
5. **Next bounded stage:** native compile/CTest/Apple Metal validation and fixes, then opt-in integration of `ComputeParticles` into native presentation/ECS bridge with explicit GPU eligibility/fallback for graph-v1 effects. Record actual benchmark numbers only after using GPU timing and representative scenes; update this same handoff with a separate commit after each bounded stage.

## Stage 45.4 — SpectraFX upstream header compile fix selected for water integration

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core` (Draft PR #23, no merge to main). **Implementation commit:** `191b22e5948b502651ec3da53777e1b9873181f7`.

**User-reported build error:** Clang reports `unknown type name ParticleSystem` in `engine/fx/VfxGraphAssets.h`, then unknown `fx::ParticleSystem`/`fx::FxHandle` in `engine/fx/SpectraFX.h`, while compiling `tests/vfx_graph_compiler_test.cpp`. These are compile-time public-header dependency failures unrelated to NEREID simulation.

**Discovery:** there is no dedicated branch named SpectraFX. Active upstream VFX development is in `feature/universal-entity-inspector-fps` (Stage 40 on its head). Compared against NEREID branch: **diverged**, 520 upstream-ahead / 356 water-ahead commits at inspection; a full merge risks unresolved CMake/editor/render conflicts and changes outside NEREID scope. The exact upstream fix was already committed as `3aa17d390fae819458cc3f89127f25804e3b2877`, changing only `engine/fx/VfxGraphAssets.h` to include `"ParticleSystem.h"` after `"VfxGraph.h"`.

**Action taken:** applied the identical upstream one-line fix to the water branch without merging unrelated commits. Verified the resulting `VfxGraphAssets.h` content is byte-identical to the corresponding SpectraFX-upstream header. This supplies the complete ParticleSystem type for inline `ps_.define`, and transitively exposes `ParticleSystem`/`FxHandle` for aliases in `SpectraFX.h`. No gameplay, VFX behavior, ECS schema, GPU code or water code changed.

**Actually verified:** GitHub branch listing, branch divergence, upstream commit diff, source comparison, and remote commit/ref update.
**NOT run:** macOS Clang recompile, CTest, graph runtime checks, VFX GPU/Metal effects, full water/FPS/RTS regression. Compilation is not claimed successful. Keep Draft PR.

**Next command:** fetch/pull `feature/nereid-water-core`, reconfigure only if required by other changes, and rerun `cmake --build build/nereid-test --parallel 4`; if it succeeds, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Report the first new compiler/runtime failure and continue in the same branch.


## Stage 45.5 — SpectraFX ECS presenter test missing snapshot vector (macOS compile)

**Date:** 2026-11-19. **Destination:** `feature/nereid-water-core`, PR #23 stays Draft. **Source:** owner-supplied Clang compiler failure at `tests/vfx_ecs_presentation_test.cpp:87-88` (`particles` undeclared).

**Cause:** The rollback-reset test added `ps.snapshot(particles)` and `particles.empty()`, but never declared the local collection. Checked both the water branch and current SpectraFX development branch (`feature/universal-entity-inspector-fps`): the same defect is present in both. This is a test-local type declaration, not a missing engine API or a water component issue.

**Fix:** Introduced `std::vector<fx::FxParticleInfo> particles;` immediately before `ps.snapshot(particles)` inside `scenario<Kind>()`. The source already includes `<vector>`, and `ParticleSystem::snapshot(std::vector<FxParticleInfo>&) const` is the public API. No runtime FX behavior, ECS state or NEREID game logic was changed.

**Verification performed:** compared the two branch files, inspected `ParticleSystem.h` public API and committed the one-line fix via the GitHub connector. **NOT RUN:** actual macOS build, CTest, GPU validation, full regression. Do not claim test PASS. **Next:** pull the NEREID branch, run `cmake --build build/nereid-test --parallel 4`; after a successful build, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Share subsequent failures; keep PR Draft until build and tests pass. **Upstream note:** the originating SpectraFX branch has the same bug, so the SpectraFX agent should also take this patch.


## Stage 45.7 — SpectraFX test regression: unsigned underflow during gameplay event check

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, PR #23 Draft. **Actual failure:** on macOS, CTest `vfx_ecs_presentation` (109/118) aborted with `std::runtime_error: gameplay event did not play`.

**Root cause:** the test compared `size_t once >= before-20`, where `before` is an unsigned particle count from a low-rate continuous emitter. When `before<20`, subtraction wraps to an enormous `size_t` value and reports a failure even when gameplay-event emission works.

**Fix:** assert the actual `impact_flash` particle ID in a headless `ParticleSystem::snapshot`: the fixture's High-quality three-particle burst, with 0.5-second lifetime, must have exactly three impact particles after one 0.1-second update. Replaying the same tick must not add another impact burst, tested after a second 0.1-second update. Existing reset check still requires no attached or one-shot particles to remain. Added `<algorithm>` for `std::count_if`. No shipping VFX, ECS or water code changed.

**Performed:** GitHub source inspection of CPU emission and presenter, guarded edit, branch commit and handoff. **Not performed:** native C++ build/CTest rerun, GPU check or FPS/RTS/PHYSIS regressions. **Next:** pull, build `aiverse_vfx_ecs_tests`, run `ctest --test-dir build/nereid-test --output-on-failure -R '^vfx_ecs_presentation$'`; if successful, full CTest. Remain Draft.

## Stage20.2 — fix observed GCC CI blocker
Implementation `1b8b58cabfe5d27b278a73344dc4894708d562e8` removes unused anonymous `validId` and the unused Editor parameter from SpectraGraphWindow::properties, including its only call. No validation behavior or UI changes: the removed helper had no callers. Exact failing CI: https://github.com/chromealex/AIVERSE/actions/runs/44733446948/job/136278777703 on STRATA source 60820d548; downloaded raw log `/private/tmp/strata-arm64-failure.log` reports -Werror=unused-function and -Werror=unused-parameter at lines 38/447. `git diff --check` PASS. Combined Mac Debug build is in progress; Linux GCC rerun NOT RUN. Next: finish build/CTest and publish branch for fresh CI.

<!-- Historical incoming Inspector/SpectraFX branch contracts; current integration gates still apply. -->
## Stage 11 — capability-routed graph runtime with lossless CPU fallback (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Aim:** provide a **genre-independent, opt-in** bridge from compiled SpectraFX graph-v1 effects to the Stage-10 Metal compute prototype **only** when the complete graph effect can run without silently dropping any modules. For all other graphs retain the original CPU `EffectDef`. No change to authoritative gameplay ECS, save/rollback, existing `RtsFx`, or production FPS/RTS clients.

### Implementation commits and affected paths

| SHA | Path | Change |
|---|---|---|
| `2f0365999a3423155db79573d461e91c3ea03c98` | `engine/fx/SpectraGraphRuntime.h` | Public `GraphRuntime`, options and schema eligibility |
| `22eb34d5c90e3b6954b3e8527b77fa972889e7f7` | `engine/fx/SpectraGraphRuntime.h` | Preserve wrapper ownership after GPU health recovery |
| `86ea5f3d76e53753c97c1600b131b2c4df8f996e` | `engine/fx/SpectraGraphRuntime.cpp` | Runtime selection, exact CPU graph fallback, startup/burst/update/render/clear lifecycle |
| `591030a4684782f3559c232c038f352154db1f71` | `CMakeLists.txt` | Link adapter to `aiverse_fx`, add `aiverse_vfx_graph_runtime_tests` / `vfx_graph_runtime` CTest |
| `cb059574a1a1cc576dfee90bdef409054d3d30f0` | `tests/vfx_graph_runtime_test.cpp` | Test GPU eligibility, unsupported module rejection, headless CPU mode, RequireGpu errors |
| `3b9a6bf70c6ef8b5404f2aabc619facea7a4638d` | `engine/fx/SpectraFX.h` | Expose native `GraphRuntime` API |
| `fe13539788dfa1fcab809f76c069d0068c981b56` | `engine/fx/SpectraGraphRuntime.cpp` | Explicit rejection of advanced modules and MinMaxCurve variants |
| `d2f0fe01adb36aef4140ab46b3180f1355414d4a` | `engine/fx/SpectraGraphRuntime.cpp` | Exact default white alpha fade required for GPU |
| `d4ac32e37ac8c1942a8316165df49fdbe7e502b8` | `engine/fx/SpectraGpuParticles.cpp` | Guard GPU clear against active render/compute encoder to prevent CPU-slot/desynced GPU buffer state |
| `c8c299e2e6507ac4399c2a131950f2fbffb8fa01` | `apps/editor/core/SpectraGraphOps.cpp` | `metalGpuEligible` / `metalGpuReason` in graph get/list/validate |
| `144c66d36702b6b4d8144021aac58c7faeeec072` | `apps/editor/windows/SpectraGraphWindow.cpp` | Show GPU schema eligibility and CPU fallback explanation |
| `eb036eb2e4a7f3a5a8cac8b1a0c7b209ee62ec17` | `tests/vfx_graph_ops_test.cpp` | Assert new MCP GPU eligibility diagnostics |
| `1c3399e779bca3423033b458202c7826a4344112` | `docs/spectrafx.md` | Graph runtime usage, strict support policy, RHI limitations |
| `6bb91ec35831143c780ebdcf35a7ad32be8b7c98` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent native C++ usage and cross-genre fallback rules |
| `b7042eedd06d0a79fe824b5a9201b22f4c6b5625` | `AGENTS.md` | Add stage-11 contracts and no-unsupported-GPU claims |

### Architecture / native C++ API

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::GraphRuntime view;
aiverse::spectra::GraphRuntimeOptions opts;
opts.preference = aiverse::spectra::ComputePreference::Automatic;
opts.capacity = 1024;
opts.quality = aiverse::fx::FxQuality::High;

if(!view.initGraph(device, graphJson, aiverse::Json::object(), opts)) {
    // reason() and errors(); RequireGpu never silently falls back.
}
// After gfx::Device::beginFrame, before render pass:
view.setView(camera, viewProjection, width, height);
view.burstAt(worldPosition, direction, tint);
view.step(1.f/60.f);
// Inside the existing depth-tested render pass:
view.draw(fog);
// Before gfx::Device destruction, after matching encoder scopes:
view.shutdown();
```

- `inspectGraphGpuEligibility(const fx::EffectDef&, GraphRuntimeOptions)` is a **pure schema check**, not a GPU capability probe. `GraphGpuEligibility {eligible, gpu, reason}` exposes an option-independent evaluation of the compiled graph's actual emitter modules. GPU v1 allows exactly one additive `builtin:soft` camera-facing point burst (1..64 particles), constant life/speed/size/drag, zero spread, only Y-gravity, default white alpha fade, and no other modules. The canonical `effectToJson` comparison against a known-supported template plus explicit complex-module/colour checks prevents dropping authoring data. Any additional emitter/output, shader graph, texture, gradient, noise, force field, collisions, sub-emitter, trail/light, random range, size curve, etc. rejects GPU.
- `GraphRuntime::initGraph(device,graph,overrides,options)` calls existing graph compiler; `initEffect(device,compiled,options)` accepts native-compiled `EffectDef` directly. For **eligible + Metal** graphs, the class attempts `ComputeParticles` and only reports `Metal` if its GPU init succeeds. For unsupported device, unsupported graph, shader-creation failure or low quality, `Automatic`/`PreferGpu` use the **original full** `fx::ParticleSystem`, not the Stage-10 simplified fallback emitter. `CpuOnly` explicitly uses CPU. `RequireGpu` fails with diagnostics.
- `burstAt/setView/step/draw/clear/shutdown` own one presentation runtime instance; production clients and editor preview are **not** transparently replaced. The internal Stage-10 GPU wrapper may fall back to its simplified CPU equivalent after a GPU health failure only for an already-compatible graph; `GraphRuntime` preserves the wrapper's lifetime and reports its actual backend.
- No authoritative ECS `World` changes; no new deterministic simulation component or cross-genre fork. Legacy `fx/*.json`, `fx/graphs/*.vfx.json`, `RtsFx` and existing C++ aliases are untouched.
- The graph editor and MCP `fx_graph_validate/get/list` now expose **schema** `metalGpuEligible` and `metalGpuReason`, which **do not confirm a real Metal device or successful GPU pipeline**. GUI preview deliberately stays CPU-only.

### Verification status (actual)

**NOT RUN:** the current container has no local AIVERSE checkout (`/mnt/data/AIVERSE` does not exist). A direct `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` attempt failed with `Could not resolve host: github.com`. GitHub connector writes/reads succeeded. No CMake/CTest, clang compilation, Metal shader compile, GPU render readback, FPS/RTS native-client integration, benchmarks or screenshots were run. Tests are committed **test sources only**, not passing results.

On the stage-11 AGENTS SHA `b7042eedd06d0a79fe824b5a9201b22f4c6b5625`, the GitHub combined status query returned an empty status list; four workflows (GPU batching regression, Studio validation, ECS experimental checks, general CI) were **queued**, not passed.

**Next agent must run:**

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then run actual Apple Silicon Metal shaders on a real GPU frame and capture images. Compare full CPU effects against all rejected graph features and the supported Metal subset, including alpha/depth and multiple frames-in-flight; exercise burst/clear/reinit, malformed graph/override, RequireGpu failure, RHI allocation/encoder failure and performance/quality tiers. The recording fake-RHI test from stage 10 does **not** execute real MSL.

### Known limitations and immediate next action

1. **Zero build proof.** Run/fix the native targets before asserting that the source compiles. Specifically inspect the new `effectToJson` equivalence gate, public includes, editor registrations and MSL compute/storage binding. CI queued is not CI passing.
2. **GPU v1 deliberately narrow.** No GPU compaction/indirect count/alpha sort, mesh/ribbon trails, collision, events, advanced graph dataflow or per-emitter materials. Current GPU draw submits all allocated slots (dead slots degenerate), capped at 4096. No benchmark or battery/mobile cost claim.
3. **Opt-in only.** GraphRuntime is not wired to production FPS/RTS host loops or native ECS presenter yet. The host must decide tick-boundary event delivery, attached transform resolution, GPU update scheduling before render passes, resource lifecycle and rollback presentation reset. Preserve existing CPU VFX parity until migration tests.
4. **Next bounded stage:** build and test stages 7–11 on macOS + real Metal, fix compiler and runtime defects with a **separate stage-12 implementation and handoff**, then opt-in one real native cross-genre presentation host using graph instances. Only after successful GPU stress/visual QA consider allocator compaction, indirect drawing and richer GPU graph execution.
5. Append to **this same living handoff** after each next code stage in a separate commit. Never replace or lose other agents' progress in `feature/universal-entity-inspector-fps`.

## Stage 12 — GPU sparse-range and device-failure hardening (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** correct two known high-risk behaviors in the Stage 10–11 Metal GPU subset — a misleading `RequireGpu` fallback after device loss and unnecessary full-capacity compute/draw work — while preserving all genre-neutral ECS/presentation and CPU paths. Add a dedicated CI compilation/regression lane. **No real Metal execution was conducted.**

### Exact implementation and validation-source commits

| Implementation SHA | Changed file | What changed |
|---|---|---|
| `ef7fa1fa969d1fb7c636105661e1ce4510b7857f` | `engine/fx/SpectraGpuParticles.h` | Stats for submitted GPU slot prefix, GPU fallback count and discarded particles; private draw-prefix state |
| `174bc522f6835ab6606b39a4e76c46dcdf2808f2` | `engine/fx/SpectraGpuParticles.cpp` | Strict `RequireGpu` failure after unhealthy device, non-seamless automatic fallback, compute/draw occupied prefix rather than full capacity |
| `edde3b227434f94616c24966675329cf0ae6fbf3` | `engine/fx/SpectraGpuParticles.h` | `ComputeParticles::ready()` exposing actual runtime-ready state |
| `279c88a770c73838d56e55bf448040dd1284f888` | `engine/fx/SpectraGraphRuntime.h` | `GraphRuntime::gpuActive()` now checks underlying emitter health |
| `e887d6b36a68b39d1495685abc096e5721bef00c` | `engine/fx/SpectraGraphRuntime.cpp` | Propagate real backend/readiness/reason after runtime failure |
| `439f8528b53cb54821fd7598a10c5e46519e00f6` | `engine/fx/SpectraGpuParticles.cpp` | Skip all GPU compute when pool empty |
| `d4c4ab4829921303daa9d1045fb5e969849184ea` | `tests/vfx_gpu_backend_test.cpp` | RHI mock: RequireGpu after device loss, explicit CPU fallback, encoder/clear safety, occupied-prefix draw count |
| `79d9e227d9482486a25cac2e33ecb1c0b7a15f25` | `tests/vfx_gpu_backend_test.cpp` | Additional staggered births/expiry across multiple frames with holes in the GPU pool |
| `cd24d96603d27807129975428bfc5443575b9760` | `.github/workflows/spectrafx-validation.yml` | PR/manual Linux+macOS focused CTest, Linux editor compile + MCP graph CTest |
| `d14f2c496fc91cf07f44c4f6d54b2da6788d3362` | `docs/spectrafx.md` | Stage-12 public API/telemetry/lifecycle/CI documentation |
| `87915882fc95bfde2f2683bdf6be1722bd736c29` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent protocol for sparse GPU draws, RequireGpu and failure reporting |
| `af80404c23e056debe7a8b07853770b8eedcfd13` | `AGENTS.md` | Stage-12 genre-neutral GPU lifecycle and no-false-claims rules |

### API, data flow, platform and compatibility

- `ComputeStats` adds `submittedSlots` (last draw-prefix capacity, including dead holes), `gpuFallbacks` and `lostOnFallback` (discarded GPU-resident particles if the GPU fails and the wrapper attempts an automatic CPU migration). Previous stats remain source compatible. `ComputeParticles::ready()` exposes the readiness bit; `gpuActive()` still requires Metal.
- Prior GPU integration/update/draw dispatches across the entire allocated `capacity`, even with one active particle. The new implementation computes the greatest occupied slot index from bounded CPU-side ages and uses that **prefix** for integrate/expand and indexed draw calls. New births are queued as before. Empty pool + no births skips all dispatches and draws.
- **Not stream compaction**: inactive slots inside the prefix still produce degenerate zero-alpha billboards; when three live particles occupy slots 3–5, `alive == 3` while `submittedSlots == 6`. This bounded optimization does not change shader, storage-buffer layout, CMake symbol, graph JSON or native ECS identity.
- `RequireGpu`: if `Device::gpuHealthy()` reports failure, `step` returns **false**, the emitter is marked not ready, its backend is not misreported as CPU, and `reason()` explains the forbidden fallback. The host must recreate/reinitialize the device as appropriate.
- `Automatic` / `PreferGpu`: runtime GPU-health failure attempts to create a CPU emitter, increments `gpuFallbacks`, increments `lostOnFallback` by the affected live count, and returns **false** for the transition frame, rather than falsely declaring seamless rendering. **An unhealthy graphics device can also make a CPU renderer unusable**: CPU fallback initialization alone is *not* proof of recovered visuals.
- `GraphRuntime::step` now copies the contained emitter's actual backend, readiness and error; `GraphRuntime::gpuActive()` also checks contained `ComputeParticles::gpuActive()`. There is no automatic FPS/RTS client replacement or mutation of authoritative ECS state.
- `clear()` inside an active render/compute encoder remains refused without altering slot bookkeeping, and outside the encoder clears `submittedSlots`. `shutdown()` resets these fields.
- Added `.github/workflows/spectrafx-validation.yml`, triggered on relevant PR changes and manual dispatch. Linux/macOS build the five standalone SpectraFX C++/RHI mock regression targets and run `ctest -L SpectraFX`; Linux also compiles the editor and runs `vfx_graph_ops`. This CI workflow **does not** execute Metal shaders on a GPU or validate Apple Silicon visuals.

### Actual tests / CI record — NOT RUN

No native AIVERSE checkout exists in this tool container (`/mnt/data/AIVERSE` absent), so there was **no actual CMake configure, build, CTest, Metal shader compilation, screenshot capture or performance benchmark**. GitHub API/connector writes succeeded and remote workflow metadata was queried.

On latest Stage-12 AGENTS commit `af80404c23e056debe7a8b07853770b8eedcfd13`, the GitHub workflow query showed **five queued** jobs/workflows: `GPU batching focused regression`, `ECS 2.0 experimental checks`, `SpectraFX validation`, `CI` and `AIVERSE Studio validation`. The combined status endpoint returned **no successful statuses**. Therefore mark nothing as PASS.

Commands for local AIVERSE agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Real Metal QA must follow:** start device frame, initialize an eligible SpectraFX graph, spawn from many locations across multiple frames, simulate, begin render pass, draw, inspect output pixels/screenshot, verify shader compile logs and GPU command ordering, test draw range and sparse holes, `RequireGpu` failure without CPU transition, automatic fallback and full device recovery, and compare with CPU quality/visuals. Run on an actual Apple Silicon GPU and track upload bytes/draw calls/GPU timings.

### Known limitations and exact next action

1. **Build status still unknown.** The new CI lane exists and was queued, but this stage did not see a completion. Its source changes are not compile-verified; next agent should fetch actual CI logs and fix real compiler/test failures first.
2. **GPU prototype remains narrow:** one additive soft-sprite emitter, no true GPU compaction, indirect count, scene depth collision, shader graph/dataflow nodes, multiple textures, ribbon/mesh effects or GPU emitter event channels. Vulkan/GL/GLES/WebGL remain CPU fallback. The prefix optimization is O(capacity) CPU age bookkeeping and can still draw holes.
3. **Runtime recovery is not seamless.** GPU-resident particles are lost on an automatic transition and `step` reports `false`; if `gpuHealthy` is false, a separate graphics-device reinitialization may still be necessary. Do not treat fallback as proof that graphics recovered.
4. **Next bounded stage (Stage 13):** observe/fix CMake+CTest and actual Metal diagnostics on Apple Silicon; implement a real offscreen Metal compute+draw smoke test and safe graphics-device recovery before considering default GPU effect routing. Once verified, design GPU compaction/indirect buffers and a genre-neutral ECS presentation scheduler that can route multiple graph emitters in native projects. Each implementation stage must end with another **separate** handoff commit here with actual SHAs/test results and explicit limits.

## Stage 13 — dense Metal particle gather with CPU-owned live index map (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** eliminate dead-slot billboards and sparse-index draw overhead in the existing opt-in SpectraFX Metal compute subset. Use a CPU-published live-index map and GPU-resident vertex gather; do not overstate this as fully GPU-autonomous compaction. No ECS/gameplay/save/rollback or RTS/FPS production client changes.

### Implementation commits

| Exact implementation SHA | File | Changes |
|---|---|---|
| `a1e8a6b2c9718c7360fc633b91a2143d1fd99a60` | `engine/fx/SpectraGpuShaders.h` | Both MSL and future GLSL compute sources gather only live physical slots into dense billboard vertices using storage buffer 4 and `meta.w` alive count |
| `09cf4b2c4cdbe8603a476e49def8df2fac7ad44e` | `engine/fx/SpectraGpuParticles.h` | Persistent `liveSlots_` GPU buffer, CPU-side `aliveSlots_` list and dense `drawCount_`; update stats semantics |
| `fb8d3a97a65cb700c21fab2a44e0aeecc979f687` | `engine/fx/SpectraGpuParticles.cpp` | Create/destroy new storage buffer, reserve live-slot list, include allocation in GPU-memory budget, reset on clear/shutdown |
| `f1aed98bd0ab40655a62ee8605e7d143260cafce` | `engine/fx/SpectraGpuParticles.cpp` | Update particle ages after compute submission; use CPU predicted survivors to upload slot map, dense compute expand and exact-live indexed draw |
| `e5816eeadc5e801cde330b2f4723b042eaae79cd` | `tests/vfx_gpu_backend_test.cpp` | Recording-RHI captures live-slot map and asserts dense draw of the 3 surviving physical slots [3,4,5] |
| `b04c9c3436caa88b1c2e084334c51473f5a152d7` | `docs/spectrafx.md` | Stage-13 design, platform limitations, test scope and latency/bandwidth caveats |
| `d30d1c217e6a5f475d945890fce77da501f9d546` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Canonical AI agent implementation/usage/validation guidance |
| `6f53180d965f2388884ab842053c7891f66d480c` | `AGENTS.md` | Stage-13 no-false-claims and CPU-preservation engineering rule |

### Architecture and source/ABI contracts

- Existing stable physical GPU state slots remain `ParticleState` (64 bytes). GPU quad `Vertex` remains 40 bytes and retains its existing sprite shader/pipeline layout. Capacity still capped at **4096** slots (16-bit indices). One new `BufferType::Storage` holds `uint32_t liveSlots[capacity]`, bound at **compute slot 4**.
- At each presentation frame, CPU scans its bounded lifespan/free-slot records, predicts the post-step survivors (`ages[i] > dt`), builds an ascending physical-slot map and uploads that map (4 bytes per live particle) along with any new birth data **before beginCompute**. Do not mutate CPU ages or free slots if the compute encoder cannot begin.
- GPU pass 0 writes births into physical state slots; pass 1 integrates physical slots through the previously-live high-water index; pass 2 reads `liveSlots[id]`, gathers resident GPU position/velocity/tint and writes to **dense** `vertices[id*4..id*4+3]`. Uniform `meta.w` supplies the live count; both Metal and GLSL source agree. No GPU→CPU position readback is required.
- The host then draws `drawCount * 6` indices from the preexisting 16-bit static index buffer; `drawCount == alive` after a successful GPU step. Dead holes in the physical pool are **never** submitted as vertex quads. The Stage-12 `submittedSlots` field now reports actual densely emitted quads (previously it was a high-water prefix, possibly including holes). Update consumers accordingly.
- `ComputeStats::uploadedBytes` now additionally accounts for the 4-byte-per-survivor per-frame map. The GPU buffer memory budget includes the added 4-byte-per-capacity buffer. Idle pools still skip compute/draw. Normal graphic output remains one additive `builtin:soft` particle material with no alpha sort and no cross-emitter batching.
- Legacy `fx::ParticleSystem` CPU backend remains the default for production RTS/FPS clients. `GraphRuntime` still only opts eligible graphs into this Metal subset. Other graphs, platforms and quality tiers preserve the full original CPU effect. ECS deterministic state and old graph assets are not touched.

### Test and CI status — NOT RUN

The recording-RHI test was **updated, not executed**: it asserts that after two groups of three births, when physical slots 0–2 expire and 3–5 live, the GPU slot map equals `{3,4,5}` and the renderer issues exactly **18 indices**, not 36. Other scenarios still cover empty-pool no-dispatch, clear safety, `RequireGpu` device health, auto fallback and detailed diagnostics. The `.github/workflows/spectrafx-validation.yml` CI workflow still exists but at the last query its jobs were **queued**; no successful build/CTest result was available for this stage. No MSL compiler or actual Metal device was used. Therefore **source committed ≠ tested** and no performance measurements are claimed.

To validate in a full native checkout:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_graph_tests aiverse_vfx_assets_tests aiverse_vfx_ecs_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then perform real Apple Silicon validation: use a Metal `gfx::Device` to compile `cs_main`; render after an initial burst, spawn a second group later, let the first expire, and verify exactly the expected visible second group (no holes/ghost quads). Inspect actual GPU buffer(4) values and `meta.w` in a GPU frame capture; measure uploads, dispatches, draw counts and frame time at 128/1024/4096 live particles. Compare visual FPS/RTS-agnostic example effects with CPU fallback. Test multiple in-flight frames and device reset before considering default enablement.

### Known limitations and next bounded stage

1. **No CI/real device proof** yet: fix any actual compiler, CTest and Metal shader issues in these source changes before extending the compute path. CI queued does not mean passing.
2. **CPU-published gather is not autonomous GPU compaction.** The CPU still scans lifetime/slots O(capacity), uploads O(alive) 32-bit indices, computes draw count and submits `drawIndexed` with an alive CPU value. GPU owns particle motion and vertex expansion only. Real GPU scan/atomic compaction + GPU indirect arguments is a separate, higher-risk stage.
3. The single Metal soft additive sprite emitter remains restricted; no GPU ribbons/meshes, depth collisions, sorting/alpha blending, multi-material/render-queue batching, sub-emitter events, scalable pooled effects or generic Niagara dataflow. Vulkan/GL/GLES/WebGL remain CPU fallback.
4. **Next stage (Stage 14):** first obtain real Linux/macOS CI and Mac Metal render/test logs. If green, design an opt-in **GPU-authored alive counter + indirect indexed draw** behind an explicit RHI capability gate and preserve the CPU list path as fallback; then build a genre-neutral multi-emitter presentation scheduler with correct rollback reset, budgeting and visual parity. Do not silently alter existing clients.
5. After every next code stage, append a separately committed handoff to **this same file** with exact SHA, tests actually executed and next work. Concurrent agents continue working in this branch; use blob-SHA-safe updates.

## Stage 14 — experimental Metal GPU atomic compaction and indirect draw (COMMITTED, NOT VERIFIED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** add an **opt-in, capability-gated** GPU-resident *vertex compact + indirect draw count* path to the already genre-neutral SpectraFX Metal subset without changing existing RTS/FPS clients, authoritative gameplay ECS, save/rollback state or the default CPU/host-gather rendering path.

### CI state investigated before implementation

Prior Stage-13 commit `cff73a55bca0e4b5d8ba62f5a5745fea441e972e` had five GitHub Actions runs (`CI`, `SpectraFX validation`, `AIVERSE Studio validation`, `ECS 2.0 experimental checks`, `GPU batching focused regression`). The Stage-13 SpectraFX run `44309690377` returned all three jobs `queued`, including macOS and Linux headless builds and Linux editor. No actionable completed compiler/test logs were available. Existing Metal RHI `drawIndexedGpuIndirect` supports a `BufferType::Storage` containing the 20-byte `DrawIndexedIndirectCommand`, independent of experimental ICB. Real Metal results **still not obtained**. Do not interpret a queued CI workflow as passing.

### Source, test and documentation commits

| Exact SHA | Files | What changed |
|---|---|---|
| `4c0313e595f8b04246106f0f97346dd2286081b0` | `engine/gfx/Gfx.h` | Default-false `supportsGpuAuthoredIndirectDraw()` RHI query |
| `a8404df859ce986d77920b50949f39a274a72851` | `engine/gfx/metal/GfxMetal.mm` | Metal advertises plain GPU-authored indirect indexed draw without enabling experimental GPU/CPU ICB |
| `f12f4a1e7e72ab163c37d9331938a8b62e4e5f7f` | `engine/fx/SpectraGpuShaders.h` | MSL/GLSL parity: atomic counter reset (mode 3), GPU physical-slot compact to dense vertices (mode 4), indirect args emission (mode 5), plus legacy modes 0/1/2 |
| `3c85f8729e3a88f618266014acf3e7572f5a09de` | `engine/fx/SpectraGpuParticles.h` | `ComputeEmitterDesc::gpuIndirect` opt-in, query `gpuIndirectActive()`, stats + counter/args storage |
| `e473153bd9be257fcf54a9b135cbd4224e241d99` | `engine/fx/SpectraGpuParticles.cpp` | Allocate and own persistent 4-byte atomic counter + 20-byte draw args buffers, account for memory |
| `ff800ab3d6e22945d82b43022599e38a106919db` | `engine/fx/SpectraGpuParticles.cpp` | GPU reset/spawn/integrate/compact/args dispatches, GPU-authored draw, strict failure handling, fallback to host-indexed GPU gather |
| `beb1a77aefa5415ae02b83c1d8eb5b9408f78e21` | `engine/fx/SpectraGraphRuntime.h` | `GraphRuntimeOptions::gpuIndirect` opt-in |
| `dc7241d77b70e84136c409788b729954b112b5da` | `engine/fx/SpectraGraphRuntime.cpp` | Forward indirect config into strict GPU eligibility/runtime |
| `d8a9424cb89f95ed633587c77a99fd9148d23b13` | `tests/vfx_gpu_backend_test.cpp` | RHI recording test verifies pass order/capability fallback, no live map upload, indirect submission failure without stale direct draw |
| `e2c5635790adc74211e694a909e066f0bedea304` | `tests/vfx_metal_compaction_smoke.mm` | Real Metal MSL compile + command dispatch + GPU counter/indirect args/vertex readback smoke source |
| `5fea0019f62600e7364f8e0b8435a43123a8872e` | `CMakeLists.txt` | Add macOS-only `aiverse_vfx_metal_smoke` and CTest `vfx_metal_compaction_smoke`, SKIP_RETURN_CODE 77 |
| `08c863583bd339bd5b9ce6bab06020dc7d6c80dd` | `.github/workflows/spectrafx-validation.yml` | Compile/attempt the real Metal compute smoke on macOS, classify no-GPU result as SKIP |
| `52312f92d2863c40acd2fe16c33afc884713e5a3` | `tests/vfx_graph_runtime_test.cpp` | Ensure option forwards through graph eligibility |
| `7af850a5762a63d353b3eec1f8624cfc20952679` | `engine/fx/SpectraGpuShaders.h` | Explicit field-by-field MSL indirect args write (safer than aggregate assignment) |
| `db09c565aac353032c7c7c0785c969f11b6b7db9` | `docs/spectrafx.md` | Public API, passes, CPU ownership and exact validation limits |
| `565032dcb67d751ab664721bd4d0b7b0ee87dd00` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent-facing GPU indirect opt-in and no-false-positive testing rules |
| `a64083b6432598d77f0fe7b537f7c6a427eaace0` | `AGENTS.md` | Mandatory compatibility and stage-14 handoff protocol |

### Native API / RHI / graphics contract

```cpp
#include "engine/fx/SpectraFX.h"

// GraphRuntime only opts into the eligible simple additive soft-sprite case.
aiverse::spectra::GraphRuntimeOptions options;
options.preference = aiverse::spectra::ComputePreference::Automatic;
options.gpuIndirect = true; // EXPERIMENTAL: false by default

// Standalone equivalent:
// aiverse::spectra::ComputeEmitterDesc desc;
// desc.gpuIndirect = true;
```

The RHI's `supportsGpuAuthoredIndirectDraw()` defaults to false; Metal exposes support through its existing plain indirect command (not ICB), while Vulkan/GL/GLES/WebGL remain CPU/host-gather fallback. `gpuIndirect=true` is a request, not proof of selection. Use `ComputeParticles::gpuIndirectActive()` and `reason()` to inspect result. The strict `GraphRuntime::inspectGraphGpuEligibility` remains unchanged: one additive `builtin:soft` point emitter with no unsupported modules. Existing genre client render paths remain unchanged.

New GPU storage slots: **5** = `atomic_uint aliveCounter` (4 bytes), **6** = a 20-byte GPU-writable `DrawIndexedIndirectCommand`; slot 4 remains the Stage-13 CPU survivor-index map in fallback. All storage buffers and the shader pipeline are allocated at init and destroyed by shutdown. The shader keeps `ParticleState` at 64 bytes and per-quad `Vertex` at 40 bytes; the 4096-slot, 16-bit index cap remains.

GPU mode dispatch ordering within one Metal command encoder:
1. `mode 3` resets atomic alive count.
2. `mode 0` writes queued births into GPU-resident physical slots.
3. `mode 1` integrates live physical positions/age/velocity for one frame.
4. `mode 4` checks GPU age validity and uses `atomic_fetch_add` to allocate a contiguous vertex ordinal for each survivor; no live-index list is uploaded.
5. `mode 5` writes an indirect indexed draw command with `indexCount = min(gpuAlive, capacity) * 6`, one instance and base index/vertex 0. Metal RHI inserts a buffer barrier after each dispatch.
6. Draw within the existing render pass through `gfx::Device::drawIndexedGpuIndirect(gpuDrawArgs, 0)`. The 20-byte arguments are **GPU-authored, not read back** in normal rendering.

The resulting compacted particle order is nondeterministic because of atomics, acceptable only for the current additive blending subset. CPU **still owns** birth/expiry/free-slot bookkeeping and computes the highest physical slot index to integrate; there is no fully GPU-resident free-list allocator or fully GPU-driven particle lifecycle. `ComputeStats::gpuCompactDispatches` and `gpuIndirectDraws` track submitted commands. In indirect mode `stats().alive` and `stats().submittedSlots` are **CPU lifespan estimates**, not GPU counter readbacks. The actual number of drawn indices comes from GPU args.

If indirect support is absent, the previous GPU compute + CPU-provided live-slot gather remains active (no default performance regression from enabling GPU indirect globally). If an indirect draw request fails **at render time**, SpectraFX reports an error and skips the draw rather than using potentially stale or uninitialized vertices for a direct draw. CPU fallback for unsupported graph modules remains unchanged and lossless.

### Verification actually performed

**SOURCE/GITHUB CHECKS ONLY; NO CMAKE BUILD, CTEST OR REAL METAL EXECUTION.** The GitHub connector wrote the source commits and returned valid SHAs. The local container still did not have an AIVERSE checkout. The latest queried Stage-14 AGENTS commit `a64083b6432598d77f0fe7b537f7c6a427eaace0` showed SpectraFX validation **pending**, with general CI/Studio/ECS/GPU batching workflows queued; no successful statuses.

The new mock test is **written but not run**. The real Metal smoke is also **written but not run**: on a macOS host it attempts `MTLCreateSystemDefaultDevice()`, compiles `shaders::kMetal`, runs modes 3→0→1→4→5, inspects GPU alive counter/indirect args and visible compacted vertex bytes, checks full expiry and physical-slot reuse. A runner without a Metal device returns code **77** and CTest marks it **SKIPPED**. That is not a positive test. A successful compute smoke still does **not** verify final rendered pixels, visual parity, batching, GPU frame times or multi-frame resource hazards.

To verify on a real macOS Apple Silicon checkout:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/spectrafx-ci -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/spectrafx-ci --target aiverse_vfx_metal_smoke aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_editor -j4
ctest --test-dir build/spectrafx-ci --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Specifically inspect actual MSL compilation of `atomic_fetch_add_explicit` and the `DrawArgs` storage layout, Metal `memoryBarrierWithScope` behavior, `drawIndexedGpuIndirect` reading a storage buffer written in the same frame, frame count/visual fade, GPU memory reuse under 3 frames in flight, empty-pool/no-draw and `clear()` behavior, `RequireGpu` device-loss handling, and RHI fallback. Collect real frame captures and compare against the original CPU ParticleSystem before enabling any default graph routing.

### Risks and exact next stage

1. The Metal runtime shader/counter/indirect path is **experimental and NOT VALIDATED**. The focused CI run was queued/pending when queried. Fetch completed CI logs and fix **actual** compile/test errors first; do not claim success until executed.
2. CPU remains the physical-slot allocator and expiry source. A fully GPU-owned particle pool would require a GPU freelist / alive scan and GPU event/birth scheduling; current Stage 14 moves **vertex compaction and draw args** onto the GPU, not all lifecycle responsibilities.
3. No alpha sorting/depth-collision/ribbons/mesh/renderer plugins, multi-material GPU batching, GPU graph function stages, system-wide presentation scheduling or cross-genre production client migration. Preserve all CPU fallbacks and old ECS components.
4. **Next bounded Stage 15:** run CI and Apple Metal smoke; fix shader/platform issues with separately committed test evidence. Then create a genre-neutral presentation-side scheduler for multiple `GraphRuntime` instances, budgets, typed gameplay event binding and rollback reset, with an optional, capability-gated indirect GPU mode. Only after actual visual/perf parity consider a GPU-resident free-list allocator.
5. As required by `AGENTS.md`, append a **separate handoff commit** to this same file at the end of every implementation stage with exact SHAs, actual test outcomes, remaining risks and concrete next work.

## Stage 14B — cross-instance SpectraFX material batching (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**User's explicit request:** combine particles that share a texture into a single draw call even when they originate from different effect instances. **Core finding:** the existing CPU `ParticleSystem::Impl::batchFor` and `FxRender::render` already combine particles across effect/instance IDs *within a single ParticleSystem*. The actual architectural gap was that separately constructed `GraphRuntime` objects own separate `ParticleSystem` objects, forcing duplicated draw submissions. A shared, genre-neutral presentation context now closes that gap without duplicating the existing FX renderer.

### Implementation, test, documentation and CI SHAs

| SHA | Changed file | Detail |
|---|---|---|
| `743d20b80f2c3479ee5695926ba35cd200ab7d96` | `engine/fx/SpectraBatchWorld.h` | New `aiverse::spectra::BatchWorld` public presentation class |
| `b2e82be8f130b217c28e79227354b71a9a3ae85f` | `engine/fx/SpectraBatchWorld.cpp` | Single CPU ParticleSystem, canonical effect-definition caching, v1 graph compilation, shared playback/update/render |
| `dd4bd9aaf1a41b019605667f7b633bbcd738d905` | `engine/fx/SpectraGraphRuntime.h` | Overloads `initGraph(BatchWorld&,...)`, `initEffect(BatchWorld&,...)` and shared-source handle bookkeeping |
| `c4f32579ab4f421d5efa3f7486bd0e2ce4b86528` | `engine/fx/SpectraGraphRuntime.cpp` | Shared world routing, source-local `clear/shutdown`, no double updates or duplicate draws |
| `cd492e7620184f179303b916f856ca7360665618` | `tests/vfx_shared_batch_world_test.cpp` | Recording RHI asserts actual cross-effect draw count and blend isolation |
| `7bec0aa4ee8181f28811b7a2da0cc3e229c5239e` | `tests/vfx_shared_batch_world_test.cpp` | Test source-local step does not update shared simulation twice |
| `60474c2b77f4b2e275d56feea9016c69be1bcc2a` | `tests/vfx_shared_batch_world_test.cpp` | Test shared graph definition caching and invalid graph rejection |
| `05e899edca85b9404a3766419c2e635ba2d1df21` | `CMakeLists.txt` | Add `SpectraBatchWorld.cpp` to `aiverse_fx` and register `aiverse_vfx_shared_batch_tests` |
| `823be7811970ebd32817cadfa4fba6eca4aaa922` | `.github/workflows/spectrafx-validation.yml` | Include shared-batching regression in focused Linux/macOS CTest builds |
| `c17c46cdfe9408cef6df40d90f939bdd688c0c64` | `engine/fx/SpectraFX.h` | Export shared batch world through canonical native API |
| `f0dfcf978149f31eaafba13df73bc2404167f701` | `docs/spectrafx.md` | Public API, material batch rules, limitations and 16-bit index cap |
| `9f81faec056c8de2ca6013e8900d129a936f988e` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent instructions for one shared world and per-source lifetime management |
| `847bfa121fca77ac0e74fd4407df6bb13554cec5` | `AGENTS.md` | Explicit rule: no per-effect CPU renderer in clients that require cross-instance batching |

### Architecture, public C++ API, compatibility

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::BatchWorld effects;
effects.init(device, aiverse::fx::FxQuality::High);
aiverse::spectra::GraphRuntime smoke, fire;
smoke.initGraph(effects, smokeGraphJson);
fire.initGraph(effects, fireGraphJson);
smoke.burstAt({0,1,0});
fire.burstAt({2,1,0});

// The host owns this, ONCE per presentation frame:
effects.setView(camera, viewProjection, width, height);
effects.update(dt);
// Inside the open scene render pass, ONCE:
effects.draw(fog);

// These source-local resets must never erase another graph's particles:
smoke.clear();
smoke.shutdown();
fire.shutdown();
effects.shutdown(); // all attached graph instances must be destroyed first
```

- `BatchWorld::defineEffect` registers the full original `fx::EffectDef`, caching structurally equivalent serialized definitions. Distinct definitions remain separate effect IDs, with independent spawn/dynamics/lifetimes. `defineGraph` compiles v1 JSON with optional overrides and returns an effect ID; invalid graphs are rejected with errors. Native modules can still directly `play`, `move`, `stop`, `kill`, `collectLights`, `clear`, `update`, `draw` and access `stats`. Texture/mesh loader callbacks can be provided before defining effects.
- The underlying CPU renderer already has a global per-system material key: actual GPU texture/atlas handle, `FxBlend`, shader-graph representation and primitive kind at draw time. Two effects with different sprite names pointing into the **same atlas**, different colours/sizes/lifetime or different instance IDs can share the **same RHI indexed draw call** when their render pipeline is compatible. Same texture plus incompatible blend/shader state must use different draws. A run of more than 16,384 quads is split due to 16-bit indices. No unsafe global alpha-sort changes were made.
- The shared path explicitly opts a `GraphRuntime` into one `BatchWorld` via new overloads. It does **not** initialize a private CPU particle runtime. `burstAt` creates a source-owned handle; `clear()` and `shutdown()` kill only that source's tracked handles. History is capped at 65,536 handles; on overflow the oldest tracked instance is killed. Graph-runtime per-source `step`, `setView` and `draw` deliberately do **not** tick or draw the shared renderer: the native host invokes the BatchWorld once per frame.
- The borrowed `BatchWorld&` **must outlive all connected GraphRuntime instances**. The host must shut down/dispose sources first. `RequireGpu` is explicitly rejected for shared CPU paths, avoiding silent downgrade. Existing standalone `GraphRuntime::initEffect(gfx::Device*,...)` remains intact for experimental GPU work, but separate GPU `ComputeParticles` still do not batch across GPU emitter instances. A compatible future GPU batcher must manage shared pools, emitter parameters/materials, source handles, rollback and draw-queue ownership.
- Existing RTS/FPS clients already use a central CPU `ParticleSystem` and benefit from its existing instance-agnostic material batching. This implementation primarily addresses *separate native/graph-runner instances* previously producing redundant calls. No authoritative ECS state, deterministic hashes, save format, old serialized FX data or production host integration changed.

### Test source and actual verification

New CTest `vfx_shared_batch_world` (target `aiverse_vfx_shared_batch_tests`, label `SpectraFX`) uses a `gfx::Device` recording mock to check:
1. Two independent `GraphRuntime` objects with **different effect definitions** (5 particles using `builtin:soft`, 7 using `builtin:spark`) share the same atlas texture; together they should produce exactly **one indexed draw of 72 indices** and `FxStats.drawCalls == FxStats.batches == 1`.
2. A third alpha-blended effect sharing the same atlas texture must produce a separate draw, for a total of **two** draw submissions.
3. Calling `clear()/shutdown()` on one graph removes only its particles, leaving the other sources alive.
4. A source-local `step()` must not double-update the shared world; the host's `BatchWorld::update()` owns all simulation.
5. Graph-v1 registration, normalized definition caching, invalid JSON rejection and explicit `RequireGpu` rejection.

**NOT RUN:** This session had no local AIVERSE checkout. A container check confirmed `/mnt/data/AIVERSE` is absent, and `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` returned `Could not resolve host: github.com`. GitHub connector writes succeeded. No real CMake compilation, CTest, Metal shader execution, Linux/macOS renderer frame or GPU benchmark was performed. On latest test SHA `60474c2b77f4b2e275d56feea9016c69be1bcc2a`, GitHub workflow metadata reported SpectraFX validation and general CI **cancelled** (other runs queued), with no successful combined statuses. **Do not mark this stage as passing/production-ready.**

Run in a real checkout:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_vfx_shared_batch_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_gpu_tests aiverse_editor -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^(vfx_shared_batch_world|vfx_graph_runtime|vfx_gpu_backend)$'
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

### Known limitations and exact next step

1. **The shared world is opt-in:** existing standalone CPU `GraphRuntime` objects still have independent renderers. The next project/native-host agent must explicitly use `GraphRuntime::initGraph(BatchWorld&, ...)` or direct `BatchWorld::defineEffect/play`. Existing RTS/FPS apps already share a ParticleSystem and need no duplicated renderer.
2. **GPU pooling not implemented:** each separately instantiated Metal `ComputeParticles` still owns its own quad material, compute state and draw call. Share a GPU pool only for effects with compatible shader, texture/material and simulation parameters; preserve per-source lifecycle, diagnostics, quality tiers, and full CPU fallback. Do not enable in production before actual Metal validation.
3. The existing CPU renderer orders alpha-like particles by its own batch/depth rules; do not claim globally correct sorting across different alpha materials without a separate order/parity review.
4. Current SpectraFX Stage 14 experimental GPU-indirect work may have unverified in-progress source edits on the same branch from the prior conversation; do not conflate that work with this Stage 14B CPU batching feature. Avoid force push or overwriting other agents.
5. **Next bounded step:** run the focused CTest and native editor build; inspect/fix actual compiler and RHI mock failures. After that, attach native project VFX presentation to the shared world, verify two simultaneous graph instances and on-screen RHI draw statistics, and develop an opt-in GPU shared material-pool scheduler with accurate `GraphRuntime` source resets. For each new code stage, append a **separate** handoff commit to this same file.

## Stage 15 — bounded cue scheduler, ECS bridge and rollback ownership (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** complete a **genre-neutral** presentation scheduler with data-driven per-tick one-shot cues, generation-safe entity attachments, bounded admission/queue/handle budgets, and rollback cleanup. Build upon the **concurrently added** `spectra::BatchWorld` (Stage 14B, see earlier entry), never create another particle simulation core. Keep all VFX graphics handles outside deterministic gameplay ECS and avoid rewriting existing RTS/FPS clients.

### Commits actually created for this stage

| SHA | File | Change |
|---|---|---|
| `ebd5640c0b70beee0c12bdb0d45a8a4acc212982` | `engine/fx/SpectraCueScheduler.h` | Generic `CueEvent`, `AttachedCue`, `CueBudgets`, `CueSchedulerStats` and scheduler public API |
| `fcb92fdc0f3815c789fdd2af7d03c336b23f400e` | `engine/fx/SpectraCueScheduler.h` | Diagnostics, bounded tracked one-shot eviction accounting |
| `375ce9651c7c5212ce4fcc51d52a72dd71b645fd` | `engine/fx/SpectraCueScheduler.cpp` | Effect/graph registration, tick deduplication, deferred queue/flush, attachment lifecycle, rollback-scoped handle reset |
| `84916eb15435b9b5149e0913455807948ffb9be1` | `engine/fx/SpectraCueScheduler.cpp` | Retire vanished sources before applying attachment capacity to new sources |
| `5185844174f77a326223c7dc76d18c5182c380e0` | `libraries/include/aiverselib/genre/SpectraCueEcsAdapter.h` | Read-only ECS source and `SimEvent` tick helpers |
| `3d21e163769b710c57a4e1170daaf38250a15b9a` | `libraries/include/aiverselib/genre/SpectraCueEcsAdapter.h` | Explicit C++ utility include for move semantics |
| `72febf114b98cdbc5a93256dda8521011208502a` | `tests/vfx_cue_scheduler_test.cpp` | Two unrelated ECS genres, generation reuse, budgets, events, rollback and shared-world ownership regressions |
| `01449af89ad9645d0d9aa2eebc4b2415f3d553d1` | `tests/vfx_cue_scheduler_test.cpp` | Verify attachment budget is freed immediately when source disappears |
| `f53d5ac388acec7309fab5455fafbf51ad240a9f` | `tests/vfx_cue_scheduler_test.cpp` | Correct tests: burst particles appear after `ParticleSystem::update()`, not immediately on `play()` |
| `c3d807be7b3ab1b9901cc60409a6d9632d311a20` | `engine/fx/SpectraFX.h` | Export `CueScheduler` from canonical public header |
| `850fba98ca63eba2a49f1b58423374d1a746fb57` | `libraries/include/aiverselib/genre/SpectraFX.h` | Export native read-only ECS cue adapter using the existing component type |
| `0e772d5a1b2f88170c444c159e8ecb78fec72f10` | `CMakeLists.txt` | Compile scheduler into `aiverse_fx` / `AIVERSE::spectrafx` and register `vfx_cue_scheduler` CTest |
| `055024ac529f967eb55fd7e17d4363f977f0da68`, `7385c2bc3a66e92d52fc27b5a7de5537ea6dbf49` | `.github/workflows/spectrafx-validation.yml` | Add shared-batch + cue-scheduler regressions to Linux/macOS focused builds; remove duplicate target entry |
| `65267f4d4d50dcd11835648f829804aa9bd352df` | `docs/spectrafx.md` | Native C++ usage, tick/attachment/rollback contract, budgets and CPU-only batching scope |
| `f5a996cdb465d21430240cbf98fe96acee478b88` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Teach AI agents how to use the scheduler with generic ECS modules |
| `7f9794b781c1b6f81b7ff50e01bec24398fd7ae4` | `AGENTS.md` | Stage-15 cross-genre scheduler and mandatory handoff rules |

### Architecture / API / serialization

`CueScheduler` references exactly one caller-owned `spectra::BatchWorld`; it creates no extra `fx::ParticleSystem`, renderer or new genre-specific component. Effect data can be registered once through `registerEffect(name, fx::EffectDef)` or `registerGraph(name, versionedGraph, overrides)`; `bindCue(moduleDefinedType, name)` maps any unsigned event type to an effect. `acceptTick(committedTick, span<CueEvent>)` accepts monotonically increasing ticks only, copies accepted cues into a bounded deferred queue and captures resolved effect IDs so later asset name changes do not rewrite pending events. `flush()` starts at most `maxPerFrame` queued one-shots; the host owns the **single** `BatchWorld::update(dt)` and `BatchWorld::draw(fog)` per presentation frame.

`syncAttachments(span<AttachedCue>)` receives all currently visible/attached source descriptions, keyed by stable `uint64_t`. It moves an existing handle, restarts it when effect/revision/scale changes, or detaches vanished sources with their stop-or-kill policy. Retiring vanished sources **before** admitting new ones prevents one-frame starvation when the attachment budget was full. `CueBudgets` limits queue length, admissions per tick/frame, attachment count and rollback-tracked one-shot handles. Safe upper bounds clamp caller-provided capacities. When the tracked cue handle ring fills, the oldest handle is **killed**, never simply forgotten, to prevent residual particles escaping the next rollback reset.

`resetForRollback()` force-kills *all handles owned by this scheduler*, clears its queued future events and presented-tick cursor; it does NOT clear particles or definitions belonging to other users of the same `BatchWorld`. After reset the host may replay ticks from a restored earlier tick. No scheduler state is written into authoritative ECS or serialization.

Opt-in native helper `syncEcsCueSources(world, scheduler, resolvePose)` reads the exact existing `lib::genre::SpectraSourceComponent` and uses `Entity::packed()` (index+generation) for source identity. It takes a caller-provided render-pose resolver instead of assuming an FPS actor, RTS unit, camera or vehicle shape. `acceptSimCueTick(scheduler, tick, SimEventSpan)` translates fixed-point `SimEvent.pos` to float visual cues. Both helpers are read-only with respect to the `World`; generic custom events with explicit tint/scale/axis can be fed directly via `CueEvent`.

**Native usage:**
```cpp
#include <aiverselib/genre/SpectraFX.h>
aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);
aiverse::spectra::CueScheduler scheduler(shared);
scheduler.registerEffect("impact", impactDefinition);
scheduler.registerEffect("engine_fire", loopingEngineFire);
scheduler.bindCue(72, "impact");                 // type belongs to the game

// After the gameplay tick commits:
aiverse::spectra::acceptSimCueTick(scheduler, tick, events);
// Per presentation frame: same shared world for ALL genre-independent cues.
shared.setView(cam, viewProjection, width, height);
aiverse::spectra::syncEcsCueSources(world, scheduler, resolvePose);
scheduler.flush();                              // spawn queued one-shots
shared.update(dt);                             // ONCE per frame
// During one open RHI scene render pass:
shared.draw(fog);                              // ONCE per frame
// On rollback (before replaying ticks):
scheduler.resetForRollback();
// Destroy scheduler BEFORE shared.shutdown().
```

### Actual verification and CI observations — NOT RUN

During this stage, GitHub source files were read and the listed commits were successfully written. **No local AIVERSE checkout/clang/CMake build, CTest execution, editor launch or Metal GPU/device smoke was performed by this agent.** Therefore added tests and code have **not** been proven to compile or pass. Source review confirmed that `ParticleSystem::play()` allocates an instance but burst particles are created by `ParticleSystem::update()`; the new test fixtures were amended accordingly. This source inspection is not a passing runtime test.

GitHub Actions run metadata for Stage-14 handoff commit `b377a83f43b8aa6b7d80591d88fc313c8d1b8a32` showed `CI` and `SpectraFX validation` **completed/cancelled**, not failed compiler logs or passing results. The stage-15 AGENTS commit `7f9794b781c1b6f81b7ff50e01bec24398fd7ae4` likewise had `CI` and `SpectraFX validation` **completed/cancelled** and Studio/ECS/GPU batching workflows queued. The active branch receives concurrent commits, so run cancellation alone is not evidence of a C++ compiler regression.

**Next agent must run with real checkout:**
```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_vfx_cue_scheduler_tests aiverse_vfx_shared_batch_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_gpu_tests aiverse_editor -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
# On macOS also:
cmake --build build/vfx-test --target aiverse_vfx_metal_smoke -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_metal_compaction_smoke$'
```

Validate native ECS behavior for more than two genres, command-line/headless context and actual editor screenshots. Ensure one continuous source moving across frames does not restart, generation reuse detaches the old source, budget limits apply during bursts, completed one-shots are owned for rollback, and a different BatchWorld producer's particles are untouched by `resetForRollback()`.

### Known limitations / precise next stage

1. **No passing build/CTest/Metal execution** yet. Do not enable new scheduling or GPU options automatically in production RTS/FPS loops before actual compiler/visual validation.
2. This scheduler is for **shared CPU particle simulation with RHI batching**. It does not batch heterogeneous `ComputeParticles` GPU compute emitters or schedule `gpuIndirect` across a pooled Metal resource. Stage 14's experimental Metal mode remains opt-in and separate.
3. The `acceptSimCueTick` helper materializes one presentation `CueEvent` per incoming `SimEvent` before `acceptTick` applies budget limits; a very large input event vector therefore has transient allocation proportional to the input. For extreme event volumes, add a bounded/lazy conversion path without breaking admission-order and drop telemetry.
4. Handle source ordering and contention in large worlds; per-tick and attachment admission are bounded, but current input iteration still walks every provided event/source. No multithreaded presentation submission or distributed world support is implemented.
5. **Next bounded Stage 16:** obtain actual CI logs, fix compile/CTest failures and add a real native-client ECS integration test that uses one shared `BatchWorld` for projectile impact and persistent vehicle/character effects, verifies rollback visual parity and counts draw submissions. Only after these gates, consider a cross-instance Metal GPU material pool and GPU memory/quality benchmarks. Continue appending separate handoff commits **to this same living file**.

## Stage 16 — typed value dataflow and module graph v2 (FUNCTIONAL CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**User's current priority:** finish functional work before tests; preserve the rule that every stage finishes with a separate handoff. **Do not claim the graph-v2 implementation is production-tested.**

### Exact implementation SHAs

| SHA | Changed path | Purpose |
|---|---|---|
| `c7fdb86b436ae6470c0c9e7b18b6f3d704e07d6d` | `engine/fx/SpectraValueGraph.h` | New pure typed value DAG with literals, exposed parameters, scalar/vector expressions and normalized-age curve sampling |
| `eca270796d32a2c2c511db23ac16c14e3183860d` | `engine/fx/SpectraGraphV2.h` | New graph v2 module/data-port frontend that lowers to existing strict v1 parser |
| `7a14ee6686b11b625794aff574cf8157e6f55b55` | `engine/fx/SpectraGraphV2.h` | Fix value-to-value links to use augmented input properties (not stale original nodes) |
| `3d8a3eb950b3ebb2f5602416c1160d576723cde6` | `engine/fx/VfxGraph.h` | Dispatch `compileSpectraGraph` to V2 for version 2, preserving all version 1 callers |
| `98ef4b1459d281b87c0d0a852aeca4bcc1a91c54` | `engine/fx/SpectraValueGraph.h` | Catch cycles even in disconnected/dormant data nodes |
| `7b25429700fdebfc1fae5d9205a33a7c0aa043a5` | `engine/fx/SpectraGraphV2.h` | Require full DAG check and lower SubEmitterModule `items` arrays correctly |
| `068851e0ccfbfb2991ec1ad4c44fe3bd415db2ac` | `examples/spectrafx/typed_noise_trail_v2.vfx.json` | Native graph-v2 example with exposed parameters, typed pins, math, normalized-age fade, noise/trail |
| `ea79f761bb33bb3db05e450a6df614e42b8d184c` | `engine/fx/SpectraFX.h` | Public latest `kGraphVersion=2`, `kLegacyGraphVersion=1` |
| `8f65c4985a75b61b8253e213202c3be3e34b7fa4` | `docs/spectrafx.md` | V2 native authoring format, limitations and examples |
| `71a059859655a5af8b45cb45410b0c2fb76e7b97` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent workflow for typed graph-v2 expressions and modules |
| `fdb796d5c657233cb2bc60694280eb48eb0af557` | `AGENTS.md` | Stage 16 architecture and documentation rules for future agents |

### Architecture/API and compatibility

Existing callers of `fx::compileSpectraGraph(Json, Json overrides, EffectDef&, errors)` and `spectra::compileGraph` now transparently accept `version:2`; graphs `version:1` follow their previous compiler unchanged. The v2 frontend and typed evaluator are header-only, portable C++, with no RHI/renderer, Box3D, FPS, RTS, or gameplay ECS dependencies. The existing `GraphAssets`, `BatchWorld`, `GraphRuntime` and editor/MCP graph compilation calls therefore all receive a canonical `fx::EffectDef`.

V2 has two link kinds: regular `{from,to}` execution module chain, and `{from,to,toPort:"path"}` typed data pins (with optional `fromPort:"value"`). `toPort` patches the copied target node's input properties before dependency evaluation. Inline `{"$node":"id"}` and `{"$param":"key"}` references work recursively in module properties. Value DAG supports bounded numeric and vector ops, comparisons, select, exposed parameter overrides with strict JSON-type matching, recursion/depth checks, finite-result bounds and rejection of disconnected data cycles. `{"$curve":"expr","samples":33}` compiles a normalized-age numerical expression to 2..64 existing CPU FxRange/size keys. It is not arbitrary runtime code or a GPU kernel.

Execution module node names: NoiseModule, CollisionModule, TrailModule, LightModule, ForceModule, VelocityModule, SubEmitterModule, TriggerModule, InheritVelocityModule, SpeedLimitModule, ColorBySpeedModule, SizeBySpeedModule, SpinBySpeedModule, LifeBySpeedModule, ShapeModule, FlipbookModule, MaterialModule, SortModule and CustomModule. They become v1 `Update` node properties using FxEffect parser's already-supported nested schemas, preserving the mature CPU physics/shape/mesh/trails/noise/rendering behavior. SubEmitterModule takes `properties.items` containing a sub-emitter array. V2 can produce multiple SpriteOutput/MeshOutput/RibbonOutput effect emitters but still uses v1's strict path topology.

**Important boundaries:** Graph v2 is a typed *authoring/compile frontend*. It does not execute a user-defined expression per particle on GPU, does not add custom particle attributes, dynamic function subgraphs, full arbitrary math shader VM, complex GPU collisions or production integration. Existing GPU eligibility stays restricted to the safe simple Metal additive-sprite subset; advanced modules retain full CPU compatibility. Old v1 assets/API and ECS deterministic hashes are not mutated.

### Actual verification status (intentionally deferred)

**NOT RUN.** Per user request, no CMake configure, native compiler, CTest, GUI launch, Vulkan/Metal shader compile or screenshot verification was performed. GitHub content API accepted source/commit updates and the example asset was written. GitHub commit existence is **not** evidence of a working build; do not mark this stage tested or finished overall.

When the user authorizes testing, first compile `aiverse_vfx_graph_tests`, editor and all `ctest -L SpectraFX`; then add focused v2 math/port/curve/module tests and a real native source example. Preserve previous v1 test parity. On real Metal, check fallback eligibility of advanced graph v2 modules separately.

### Next bounded FUNCTIONAL stage

**Stage 17:** upgrade the ImGui SpectraFX editor to use graph v2 as its normal new-asset format, add value/data-node creation, typed data-pin linking and value inspector, curve/gradient tools and graphical module insertion/sequence reordering, plus MCP graph-v2 authoring helpers. Maintain original v1 view/edit support. Then continue richer runtime function graphs and GPU capabilities. Every finished stage must append a separately committed handoff entry to this file, with exact source SHAs, deferred-test status and concrete next step.

## Stage 17 — visual graph v2 authoring and safe drafts (FUNCTIONAL CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Scope:** complete the initial graph-v2 editor authoring workflow without changing ECS simulation, GPU performance policy, or the existing validated-published-effect contract. The user explicitly requested functionality first, tests later; continue doing separate code and handoff commits per stage.

### Implementation SHAs

| SHA | Files | Change |
|---|---|---|
| `0022b7b7d4a037a2f12fb265e3cea716f5928567` | `engine/fx/SpectraAuthoring.h` | New reusable pure C++ graph node catalog, default v2 graph, execution/data link editing, cycle protection, node creation/lookup |
| `c16ef4c54d31f6f9647debe11d0e3982a5fc0053` | `engine/fx/SpectraAuthoring.h` | Align spinBySpeed/lifeBySpeed presets with strict FxEffect schema |
| `2df0128a720391facd3c9b19a7a9c688534e12cc` | `engine/fx/SpectraAuthoring.h` | Guard malformed graph arrays before node insertion |
| `d1c7faf075667aedac4300f4ca1a31b9286ef9e2` | `apps/editor/windows/SpectraGraphWindow.cpp` | New v2 node catalog, visual typed data links, connection property selector, v1-compatible old canvas |
| `9f17d352600f9342fd8690edc574586b7a983e98` | `apps/editor/windows/SpectraGraphWindow.cpp` | Numeric/bool/vector/color literals, curve/gradient time/value and RGBA editors |
| `4ab6bdc407070060131d3e9b0ab5c59b1a1481c5` | `engine/fx/SpectraModuleSchema.h` | Common typed fields for noise/collision/trails/light/force/velocity/shape/sorting/material/trigger modules |
| `f7f96893a6f02cb19d4ce196dbf0cac1073e6517` | `apps/editor/windows/SpectraGraphWindow.cpp` | Typed light color picker |
| `81b01b3485a6d8f4d0a535d94e2876bac39b583c` | `apps/editor/windows/SpectraGraphWindow.cpp` | In-memory, nondestructive v1→v2 upgrade |
| `43c02ccd8df69b795385f9502ddfa430d0df508c` | `apps/editor/core/SpectraGraphOps.cpp` | MCP catalog/edit and private draft get/set/list/publish operations |
| `47ebb187cc57b35075be457248168be8642e3328` | `apps/editor/windows/SpectraGraphWindow.cpp` | Draft list, draft load/save, explicit publish in the editor |
| `4186efd0db7cf62fd33b2e092a77cfb52ec7268f` | `engine/fx/SpectraFX.h` | Expose common SpectraAuthoring model to native C++ |
| `98d6152692167faad3f388cb155637171e410937` | `docs/spectrafx.md` | Editor and MCP graph-v2 authoring documentation |
| `5e294f5024f9acccabac058a57ba21ec32c9143f` | `skills/visuals/aiverse-spectrafx/SKILL.md` | AI agent graph authoring instructions |
| `aa774d198539f5bf656c79cbcc741267b8c02a0d` | `AGENTS.md` | Long-term ownership, dataflow/editor and draft publication rules |

### API, architecture and asset contract

`spectra::authoring::nodeCatalog()/newGraphV2()/addNode()/connectExec()/connectData()/disconnect()/inputPorts()/node()` live in the pure `engine/fx/SpectraAuthoring.h` with no ImGui, RHI, ECS or genre dependencies. They are exported from the canonical `engine/fx/SpectraFX.h`, so a native game-specific AI or content tool can construct v2 graphs without writing a new parser.

The SpectraFX ImGui window creates v2 assets by default, opens existing v1 assets unchanged, permits a no-write v1→v2 migration, displays grouped value/math/vector/logic/stage modules and distinguishes data-port edges from execution links. The right property inspector has exact data input port selection, typed controls for common emitter modules and editable Curve/Gradient value nodes. Unsupported/complex fields retain their full JSON content.

New MCP operations:
- `fx_graph_node_catalog` returns node types, groups, starter JSON and input names.
- `fx_graph_edit` edits a graph in **memory only** (new, upgrade, add, connect, disconnect, deleteNode, setProperty), returns the modified graph and compiler diagnostic object; deliberately supports temporarily invalid intermediate authoring.
- `fx_graph_draft_set/get/list/publish` store incomplete graph JSON under `fx/graphs/_drafts/<safe-name>.vfx.json`. Drafts never enter production asset registry and never replace published graphs. `draft_publish` writes `fx/graphs/<name>.vfx.json` only after successful full graph compilation, preserving the previous published effect when invalid. Existing strict `fx_graph_set/get/list/validate/bake` remain active for both versions.
- In-editor **Черновик** saves incomplete work; **Опубликовать** validates and publishes; draft and published names have separately selectable listings. The normal editor preset is v2 but older v1 files are accepted by the same public compiler.

No deterministic ECS component, physics, renderer handle or network code was touched. No accidental genre-specific dependency introduced. Graph v2 still lowers to existing CPU FxEffect, and advanced modules continue to use CPU fallback until GPU parity exists.

### Test status (as requested: DEFERRED, NOT RUN)

No local AIVERSE checkout was made and no native compilation, GUI run, CTest, CI success check or real Metal rendering was performed. Source commits are **not** proof of compilation/GUI usability. User explicitly asked to finish functions before tests. Do not change the status to PASS without executing a test. In the future, validate the v2 catalog/MCP draft roundtrip and editor data pin wiring/undo, use both valid and incomplete graphs, test v1 migration and compiled v2 GPU eligibility, and verify that published effects remain intact on invalid drafts.

### Known limitations and next FUNCTIONAL stage

1. The editor uses ImGui direct-mode drawing with a typed property selector, not a sophisticated full-sized Niagara pin grid or searchable node library. Curves/gradients can edit keys, but do not yet provide tangent/Bezier manipulation or shared function libraries.
2. The compiler evaluates static math and age-sampled curves at compilation. It does not execute arbitrary user-defined particle attribute bytecode or per-particle GPU expression kernels; expressive full runtime graphs and user-defined functions remain open.
3. The MCP draft path is intentionally outside published asset files. Production `GraphAssets` must never read from `fx/graphs/_drafts` by default.
4. Explicit next Stage 18: add reusable typed function subgraphs with bounded call graphs, parameter binding, function-library authoring and native runtime integration, then add custom per-particle attributes/program stages as a separate bounded feature. After each code stage add **another handoff commit** to this file. Preserve all concurrent branch modifications.

## Stage 18 — reusable typed function subgraphs and function assets (FUNCTIONAL CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** share one typed mathematical graph implementation across multiple VFX effects without copying nodes, and expose typed function libraries to native projects, editor and MCP. Continue keeping the compiler pure, no genre-specific simulation, and protect published graph assets. User requests **functional completion before tests**.

### Exact implementation SHAs

| SHA | Path | Implementation |
|---|---|---|
| `157528399ad65f7f3dcf4a8062df5bd6a2fb614b` | `engine/fx/SpectraValueGraph.h` | New `FunctionCall` value node, argument resolution, nested bounded function evaluation with shared recursion guard and output type checking |
| `31e7aa70516ddf1c5d93d40ad5cadfcaa501872d` | `engine/fx/SpectraGraphV2.h` | Root `functions` support; validate dormant function definitions, signatures, call depth and cross-function cycles before compilation |
| `c2223e69a25557057efab751220cc18bb3ec7d4c` | `engine/fx/SpectraFunctionAssets.h` | Pure library package validation/import and externally injected reader + last-known-good function-asset reload |
| `a46a9ca4de67632b57b7a23a4b85b5400102236a` | `engine/fx/SpectraFX.h` | Export function-library facilities from the supported native entrypoint |
| `f10b0a81b859e75606c377107bfdd420e9756f59` | `engine/fx/SpectraAuthoring.h` | FunctionCall authoring node and input-pinning metadata |
| `a159afb9ed18eb95d9a2a1cef8f7e341f984866b` | `examples/spectrafx/common_math.vfxfn.json` | Version-1 reusable function package with speed scaling and age fade |
| `15537ae4c692d18244e998fe482c5af25cefbbb1` | `examples/spectrafx/function_calls_v2.vfx.json` | Full v2 example with reusable function calls in static emitter values and age-dependent sampled curve |
| `92d49954c70b10a1b7233228ce7b914ef5f83aac` | `apps/editor/core/SpectraFunctionOps.cpp` | MCP `fx_function_list/get/set/import/export`; safe name validation, strict library compile and explicit graph import |
| `26bc07e434325278a62a61014865f43274a61c7e` | `apps/editor/core/SpectraGraphOps.cpp` | Register function-library operations with existing graph MCP entry |
| `94c4c5c2394fe5b2699a08b2af3d63244f207d50` | `CMakeLists.txt` | Compile new functions operation translation unit into `aiverse_editor_core` |
| `6aba20a9dca913984e62049cdb342cee95e85512` | `apps/editor/windows/SpectraGraphWindow.cpp` | Root function JSON editor, FunctionCall selector, library import/export controls |
| `6b4080db8c0ad635eb01d3ad3c6de1bb714df3c3` | `docs/spectrafx.md` | User-facing function data format, import and native library API |
| `e6da52b27fcc68683ea0313603c431337f2e8e66` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent recipe for reusable function graph creation |
| `dfd76786247c959244ed5446a5a75867b2e327b8` | `AGENTS.md` | Functional constraints and next-agent role |

### Architecture / API / serialization

Graph v2 root may contain `functions: {name: FunctionDef}`. Each `FunctionDef` contains:
- `parameters` object of strictly typed default input values,
- `nodes` array of ONLY pure typed value nodes (not emitter/renderer stages),
- `output` data node ID and optional `returns` type (number/bool/string/vec2/vec3/vec4).

`FunctionCall` value nodes refer to `properties.function` and `properties.arguments` inputs, which may themselves contain literals, `$param` or `$node` references. A function executed from a `$curve` expression inherits the normalized-age sampling context, allowing reusable fade/force/math subgraphs to compile into the established CPU FxRange keys. Up to 32 function definitions per graph, up to 64 value nodes each, and maximum nested call depth eight. The V2 compiler rejects missing functions, recursive calls (including unused functions), bad signatures and wrong return types, preserving the original strict FxEffect compiler.

Standalone reusable `fx/functions/<name>.vfxfn.json` assets have `version:1,functions:{...}`. `spectra::validateFunctionLibrary` validates them with the real graph compiler; `spectra::mergeFunctionLibrary` copies validated definitions into a graph-v2 authoring object, rejects conflicting function definitions unless explicitly replaced, and works with incomplete parent graph drafts. `spectra::FunctionLibraryAssets` injects its file reader and caches last-known-good versions by content hash; the core graph compiler retains NO filesystem/network access.

Editor/MCP `fx_function_list/get/set/import/export` allow agents to create and share validated libraries; imports produce an in-memory edited graph that needs explicit publish/draft-save. The ImGui graph editor now offers root `functions` JSON editing, a FunctionCall dropdown of declared names, and named import/export. No changes to authoritative ECS, network state, gameplay or CPU particle draw pipeline.

### Verification status — intentionally NOT RUN

User explicitly requested implementing functionality first and tests later. No native CMake configure, compiler, CTest, Metal/Vulkan shaders, GUI smoke, CI success or production RTS/FPS parity check occurred in this stage. GitHub content commits were recorded; that proves source storage only. **No PASS claims.**

Later regression matrix must cover:
- nested function calls and valid output type, `$curve` with `AgeNormalized` inside functions, overridden parameter compatibility and graph v1 behavior;
- missing/recursive/dormant mutually recursive function graphs, >8 call depth, unknown return types and malformed library assets;
- last-good library hot reload on missing/invalid new source, conflicting import reject vs explicit replace, editor/MCP import/export and draft/publish safety;
- native C++ include order and builds on macOS/Linux/Windows.

### Known limitations and next functional stage

1. Functions are **pure compile-time expressions or normalized-age sampled curves**; they are NOT a general executable particle bytecode, user-defined runtime shader stage or material function compiler. No arbitrary game scripts run here.
2. Library assets import explicit definitions by copy. Production graphs do not auto-read external libraries at runtime; the caller may import before compilation. Function graph output is currently one typed value (use Vec3/Vec4 for multiple related components).
3. Full Niagara feature parity still needs user attributes, true per-particle runtime Update/Spawn/Event programmable stages, GPU codegen, multi-material compute pools, and richer graphical function/node editors. Previous experimental Metal GPU mode remains strictly opt-in.
4. **Next bounded Stage 19:** implement a typed, bounded **per-particle expression program IR** with CPU interpreter and spawn/update/event module hooks, then connect graph-v2 particle program declarations to the shared CPU particle runtime using optional per-emitter programs; ensure existing particle behavior is unchanged without a program. Continue handoff after each code stage with actual SHA and test status.

## Stage 19 — compiled runtime per-particle expression VM (FUNCTIONAL CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** close the gap between compile-time-only typed v2 graph expressions and genuine **per-particle runtime** effects by adding a bounded, pure CPU program with Spawn/Update/Death/Collision/Trigger hooks and eight custom scalar attributes. User explicitly asked to finish functions before running tests; do not claim native compilation or performance parity.

### Exact source and documentation SHAs

| SHA | Path | Change |
|---|---|---|
| `f2b3843cdb934d019d86935bceb27cbc1da77b56` | `engine/fx/SpectraParticleProgram.h` | New pure typed scalar expression VM: bounded compilation, fixed-stack evaluator, field reads/writes, statically limited stages/instructions and finite-value guards |
| `fba221e5cf7f099eb109134b334bbdf9497a3667` | `engine/fx/FxEffect.h` | `EmitterDef::particleProgram` optional JSON definition |
| `bcf3bea4afa54ac8cbbb2ac4793ac104a0e8b8a8` | `engine/fx/FxEffect.cpp` | Validate program during strict effect parse and preserve in stable `effectToJson` roundtrip |
| `396800d17de302ed1bc45703a9dc9cbc5c861ef5` | `engine/fx/FxInternal.h` | Compile-only `EmitterRT::particleProgram`, 8 per-particle visual attributes and `Impl::runProgram` helper |
| `d1676c5beede74856b7fdc3575295897b576625b` | `engine/fx/ParticleSystem.cpp` | Compile VM once per emitter, execute per-particle Update, Death, Collision and Trigger stages at correct lifecycle boundaries |
| `acddb8e3dd15949eeaf8195af87ac726c894ff30` | `engine/fx/FxEmit.cpp` | Execute Spawn program once after initial particle values/uid and before trails/birth events |
| `fa3b99258599948fdaa73506251c247572963687` | `engine/fx/SpectraGraphV2.h` | `ParticleProgramModule` lowering to effect's nested `particleProgram` field |
| `1acd0ef6cb861f4738ebf4f182bd533206f63ae4` | `engine/fx/SpectraAuthoring.h` | Authoring catalog for program module in v2 visual editor |
| `298b2fa9c880e81b4f477092108ec0ef358e5ad6` | `examples/spectrafx/runtime_particle_program_v2.vfx.json` | Full effect sample: user attribute, age-rising force, fading alpha, changing size, death hook |
| `618d65e764532c0ff5961194da748d5acba2f994` | `docs/spectrafx.md` | Program format, runtime event timing, VM resources and fallback constraints |
| `fc6a097052c56bd6587f7741ba5fcafa6845b91b` | `skills/visuals/aiverse-spectrafx/SKILL.md` | AI-authoring recipe for per-particle programs |
| `16e8b57cd11c22f692c8f58759f0e64b5c9bf1b0` | `AGENTS.md` | Strict precompiled VM use and genre independence contract |

### Runtime API / schema / compatibility

Per emitter `particleProgram` JSON has `{"version":1, "spawn":[...], "update":[...], "death":[...], "collision":[...], "trigger":[...]}`. Each stage accepts a bounded list of statements of the form `{"op":"set|add|addRate|multiply","target":"writable.scalar.field","value":expression}`. Expressions are numbers, scalar field references `{"var":"ageNormalized"}`, or bounded trees of built-in arithmetic/comparison/trig operations (`add/subtract/multiply/divide/min/max/pow/sin/cos/abs/negate/sqrt/floor/ceil/saturate/clamp/lerp/select/greater/less/equal`). Readable fields: `age`, `ageNormalized`, `dt`, `uid`, `position.x/y/z`, `velocity.x/y/z`, `size`, `rotation`, `tint.r/g/b/a`, `attribute0..7`. Writable fields exclude time/age/uid and include all position/velocity/appearance/custom attributes. `addRate` integrates the value with timestep `dt`. Nonfinite or excessive values are rejected or not written, tint channels clamped 0..1.

The JSON program is validated during `fx::parseEffect` (so invalid program definitions fail the normal published asset gate), serialized by `effectToJson`, and compiled **once** to numeric bytecode in `ParticleSystem::Impl::EmitterRT`. Runtime evaluation uses a preallocated/fixed local stack, no JSON interpreter, heap, reflection, external IO or game-specific dependency per particle. Each particle owns a fixed array of eight custom scalar attributes, used to store a value in Spawn and reuse in Update or event handlers. `Impl::runProgram` adapts existing particle fields to the VM. The VM is presentation-only and never modifies deterministic ECS.

Hook timing: Spawn after built-in attribute/position initialization and before trails/birth events; Update after standard gravity/forces/integration but before physics collision/trigger checks; Collision after a real contact is resolved, before queued sub-emitters; Trigger on activation/enter/exit conditions; Death on regular eventful particle demise before its final release. Effects with no `particleProgram` use the existing `ParticleSystem` update path unchanged (no VM execution and no authored behavior change).

V2 `ParticleProgramModule` is a stage-chain node whose `properties` become emitter `particleProgram`; the whole compiled effect still uses the strict existing CPU `FxEffect` schema. Native `GraphRuntime` must reject this advanced effect from limited Metal GPU eligibility and use the full CPU fallback; no GPU VM or user-provided shader generation is claimed. Old graph v1 and old FX JSON remain source compatible. No CMake/source target registration is needed (new header-only VM included from existing .cpp files).

### Verification status — NOT RUN (per user)

No local compiler or CMake/CTest execution, Metal GPU runtime test, editor launch or native visual parity run. The user explicitly postponed tests until functional completion. The GitHub connector wrote files successfully but **there is no passing test result to report**. When eventually testing, cover:
- compile-time rejection of malformed program stages, recursive expressions, unknown targets, NaN/Infinity, invalid ops, too many statements/instructions;
- zero-alloc hot path and CPU Update/Spawn/death/collision/trigger semantics with real `ParticleSystem::snapshot`;
- custom attribute continuity over frame time, addRate dt scaling, tint/size modification, and no behavior change for effects without programs;
- v2 graph compiler lowering, strict serialization roundtrip, CPU fallback vs Metal eligibility, multi-genre native project imports and CTest/GUI integration.

### Known limitations and next FUNCTIONAL stage

1. VM is a bounded **scalar** register/stack program with per-particle custom scalar attributes, not an arbitrary HLSL/MSL/GLSL shader compiler, GPU bytecode, user C++/Lua script or complex material function graph.
2. Program expressions are supplied as declarative JSON. The v2 visual node editor can create `ParticleProgramModule` and edit its JSON, but compiling arbitrary V2 **data node DAG** automatically into this bytecode has not yet been integrated. Do not confuse static `{"$node"}` evaluator with runtime VM inputs.
3. Runtime results affect position/velocity/tint/size/rotation, but separate arbitrary vector attributes and renderer shaderGraph binding of custom attributes are not yet implemented. Existing editor preview remains CPU.
4. **Next bounded Stage 20:** add V2 data graph-to-runtime program codegen / authoring APIs and per-effect/per-instance parameter overrides without re-creating shaders. Then extend GPU compute handling of compatible program operations behind opt-in backend checking, and integrate with material attributes/renderer. Keep a separate handoff after each stage, exact SHAs and tests-deferred record.

## Stage 20 — named runtime instance parameters after merging main (SOURCE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** continue **functional** SpectraFX development, honoring the user's explicit instruction to **merge current `main` into the working feature branch first** and defer tests until functions are implemented. Implement genuine per-instance scalar inputs for particle VM programs and graph-v2 named parameter declarations, not a new FPS/RTS renderer.

### Merge of main (performed BEFORE editor changes)

- Current `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and the feature head `b070d99cd1a19e0d26ca419e13caef2c9ea9178a` at merge preparation. They had diverged (main 325 commits ahead of feature, feature 255 commits ahead of main), so fast-forward was impossible.
- GitHub PR **#33** was opened from `main` to `feature/universal-entity-inspector-fps`. GitHub reported initial conflicts. All 45 changed files from `main` were incorporated using the actual main blob SHAs into a feature-base tree. Three overlapping files were three-way merged: `AGENTS.md` (three independent InputFlow additions), `CMakeLists.txt` (three InputFlow link updates), `skills/catalog.json` (InputFlow summary/source updates). SpectraFX content was preserved.
- A real Git **two-parent merge commit** `463f69f230f7d02cdaca4c824b014ff1221402c1` was created with feature and main as both parents and pushed by optimistic ref update (**no force**). GitHub compare `main...feature/universal-entity-inspector-fps` afterwards returned **behind 0 / ahead 256**, confirming that all main commits are now ancestors of the feature head. GitHub reported PR #33 **closed, merged**. No build, CI or CTest result was produced by this merge.
- All subsequent Stage-20 commits below were applied **on the merged branch**, not `main`. Continue doing so and preserve other agents' work with fresh blob SHA reads.

### Code commits — exact SHAs

| SHA | File(s) | Implemented change |
|---|---|---|
| `2b92d8bc0ab93e85b393dc53a41e853858b25e73` | `engine/fx/SpectraParticleProgram.h` | Read-only VM fields `param0..param7` and optional 8-float instance data in Context; writable fields remain unchanged |
| `dadca56e3d37388d6477908a5aa26ac9e8be0fb7` | `engine/fx/FxInternal.h` | VM scalar array per Instance and per spawned Particle, linked to read-only execution context |
| `cb3d6ebad3590b6fffcec25b41fc82442f2d2dd5` | `engine/fx/FxEmit.cpp` | Copy instance inputs to new particles prior to Spawn VM code |
| `4e19ed2b73877115d0d113130616422ccb0826f1` | `engine/fx/ParticleSystem.h` | `setProgramParameter(handle,slot,value,affectExisting)` public API |
| `b1f4a1db6ab1be29f6c21740cbfb7c523f7159ab`, `2960c3a8ca0b7dfede9444776ab81bc02e233b35` | `engine/fx/ParticleSystem.cpp` | Update specific instances and optionally surviving live particles, even when a one-shot's instance stopped emitting |
| `255c1abc75983dcf748eed8d48db919248be38dc`, `1173bd01faf0a5f7f1ed02970baebe47c699b5c0` | `engine/fx/SpectraBatchWorld.{h,cpp}` | Forward per-handle parameter changes to the shared CPU particle renderer |
| `d85a3ace7afc99915d858330fd1ead291264a1c3`, `dbeb8d7e17086748ad2c20dacaf601f119077067` | `engine/fx/SpectraGraphRuntime.{h,cpp}` | Per-source scalar state; changes affect future births or already-owned live particles, with handle isolation |
| `4f3714b9bb88e45f3b4b9d5c3cbc3a59f09fcac4`, `f1903c0458624541470ccc8798c6d245196bb82e` | `engine/fx/SpectraCueScheduler.{h,cpp}` | Optional per-cue and attached-source 8-float parameter arrays and masks; live source parameter updates do not trigger emitter restart |
| `b070d99cd1a19e0d26ca419e13caef2c9ea9178a` | `engine/fx/SpectraCueScheduler.cpp` | Apply only masked slots and default unbound slots to zero |
| `3ea55b70c366e113fd2d9c371b56ca46944a32ff`, `6f637e9bd6a7e8eec42ca58de858ab22732e7a72` | `examples/spectrafx/per_instance_parameters_v2.vfx.json` | Complete sample using dynamic `lift` and normalized age in a ParticleProgramModule |
| `c6147cf554cd5928eb8c8c983c43ff973823c992` | `engine/fx/SpectraInstanceParameters.h` | Declarative named parameter table, unique numbered slots 0..7, initial/min/max finite validation |
| `94fa61e3f283f679c72776005055fa8d9b1db6c6` | `engine/fx/SpectraGraphV2.h` | Allow and validate `instanceParameters` root; normalize `InstanceParameter` typed nodes into numbered runtime slots |
| `3ab2d9ab808d0ea0739a650d2d91239e3ed1d348` | `engine/fx/SpectraValueGraph.h` | Distinguish runtime-only `InstanceParameter` from compile-time value graph literals |
| `0e03e7565ac0796b158b354b7b1fef8ec97f3557` | `engine/fx/SpectraRuntimeCodegen.h` | Lower dynamic named value nodes to read-only scalar VM `param0..param7` fields |
| `7dccddd81d75326760343b77a23fde6569b52d96` | `engine/fx/SpectraAuthoring.h` | Visual node catalog now contains `InstanceParameter` |
| `75d6683e4132a8308b84260daf748695077febdc`, `a418ac447ab649cecc2095d2284322c3ecb45880` | `engine/fx/SpectraGraphRuntime.{h,cpp}` | Strict `setNamedProgramParameter(name,value,affectExisting)` API and default named parameters, CPU fallback and RequireGpu rejection |
| `adf4bf06d8a80bf8214f07506eea8d2226ec57ee`, `c96d888a6d46a8d01cd4c855b138e60087e3b8ab` | `engine/fx/ParticleSystem.{h,cpp}` | `playWithProgramParameters` initializes all eight floats BEFORE prewarm/spawn; old `play()` ABI delegates using zero defaults |
| `341a271f55a007be0b9a2385d607ee216e7cc064`, `9a1392c33478fdbb574f62095aba8cd4362957a6` | `engine/fx/SpectraBatchWorld.{h,cpp}` | Same prewarm-safe shared-play API |
| `04b89c8115e6666132a0b9c587c9c46b3726a446` | `engine/fx/SpectraGraphRuntime.cpp` | Use atomic prewarm-safe playback for standalone/shared instances |
| `34c0079eb25d66a461644676fd1138e8c2150e0d`, `b070d99cd1a19e0d26ca419e13caef2c9ea9178a` | `engine/fx/SpectraCueScheduler.cpp` | Cue/attached source playback with parameter values known at spawn, respecting masks |
| `f1955363903f018b2858c5596238f7d2e2fcaa19` | `apps/editor/Editor.h` | Preserve 8-float preview instance inputs through loop restarts |
| `cc49dfbdc9fc03df518e9943637ccfa7548065ea` | `apps/editor/EditorFxOps.cpp` | `fx_graph_preview {graph,instanceValues?}` validates named overrides and spawns prewarm-safe CPU preview |
| `0ea76f6f372ecbc5a770760e56f058ecfa41379b` | `apps/editor/windows/SpectraGraphWindow.cpp` | Version-2 right-panel schema editor, valid named slider controls and preview overrides |
| `bacb4b360c830c0dd26ad704c8afc4d2951d2d33` | `docs/spectrafx.md` | Native API, graph schema, editor preview, CPU/GPU boundaries and deferred checks |
| `6016f05c6a7f849b32ecc02ed89eb763e8bd1537` | `skills/visuals/aiverse-spectrafx/SKILL.md` | AI agent instructions and usage examples |
| `64b94418009f5544b952159090c25076dce83d92` | `AGENTS.md` | Per-stage responsibilities, `main` merge record, no-false-validation claims |

### Runtime semantics and limits

- Graph v2 root `instanceParameters` maps up to 8 names to `{"slot": 0..7, "default": number, "min": number, "max": number}`. Names are constrained to safe alphanumeric/underscore length <= 64; slots must be unique; all values finite and bounded; default must lie inside range. These values are **separate** from compile-time graph `parameters`.
- `InstanceParameter` typed value node refers to the named schema and is lowered into read-only VM field `param0..param7` inside `ParticleProgramModule`. V2 compiler must never constant-fold it. The fixed-budget CPU VM reads the value from each particle's snapshot of the source's parameter array. No custom execution source, Lua, I/O, gameplay ECS or renderer shader handles are added.
- Each `ParticleSystem::Instance` holds eight floats. They are copied into each new particle at spawn, so a source's parameter changes affect **future** births by default; `setProgramParameter(..., affectExisting=true)` additionally updates all live particles matching the source's generation-safe FxHandle, even after the emitting instance has finished. `playWithProgramParameters` sets the array at allocation and before any `prewarm` or `Spawn` program, solving prewarmed-parameter timing.
- `spectra::BatchWorld` and `GraphRuntime` propagate this API for shared/standalone CPU sources. `GraphRuntime::initGraph` applies declared named defaults, and `setNamedProgramParameter` validates the name and range. Graphs with named instance inputs are forced to full CPU; `RequireGpu` refuses to omit VM behavior. Legacy graph v1 and `play` API remain supported.
- `CueScheduler` accepts optional masked per-cue/per-attached-source values. Continuous source parameter edits do not restart their emitters. All values are presentation-only and never enter deterministic ECS checksums or saves.
- The graph editor can edit named schema metadata and per-preview-instance scalar values, and `fx_graph_preview` accepts `instanceValues` without writing fx assets. Looped preview retains the same numeric inputs on subsequent spawns.

### Verification

**NOT RUN BY USER INSTRUCTION:** No CMake, clang, CTest, editor GUI automation, macOS Metal rendering, CI success or visual/performance verification was performed for Stage 20. GitHub API returned commit SHAs for successful writes and a merged pull request. This is source-level functionality only; do not call the VFX feature production-ready.

When the user later requests tests:
```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_cue_scheduler_tests aiverse_vfx_shared_batch_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```
Check prewarm timing, future vs existing particle updates, two different source values from same definition, schema range/type rejection, editing and loop preview, mask removal, rollback and CPU/GPU fallback decisions. Only after CPU parity investigate opt-in GPU VM codegen and Metal device smoke.

### Remaining FUNCTIONAL work / next bounded stage

1. A fully general GPU implementation of the CPU expression VM (including custom scalar attributes, per-instance buffers and all stage hooks) is still missing; the Metal subset remains conservative. Unsupported effects **must not** lose semantics on GPU.
2. General renderer custom particle attributes/shaderGraph bindings and material-node graphs are not yet present; connect VM outputs to compatible renderer data and add authorable attributes without duplicate genre systems.
3. The native ECS presenter and editor graph tools still require complete production client wiring/feature polish and actual visual parity. The source is not a complete, verified Unreal Niagara replacement.
4. **Next functional Stage 21:** implement GPU-compatible VM subset/parameter buffers with strict capability diagnostics and CPU fallback for unsupported expression programs, then renderer custom attributes and material graph bindings. Continue maintaining the handoff after each stage. The user expressly postpones tests until feature completion; record NOT RUN.

## Stage 21 — custom VM attribute to shader graph channel bindings (CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`, after verified two-parent merge of `main` commit `463f69f230f7d02cdaca4c824b014ff1221402c1`.
**Goal:** make computed per-particle `attribute0..7` and per-instance `param0..7` values available to the existing particle shader material graphs, without increasing the vertex payload, creating separate genre renderers or claiming a complete GPU VM.

### Exact commits and files

| Commit | Path | Implemented |
|---|---|---|
| `ce358823c2cd00943f377d536f9bf5ad143d3a26` | `engine/fx/FxEffect.h` | Optional per-emitter `Json graphInputs` metadata |
| `acbf70ab19a73a3ef33ce1f8085f38672f4f2a34` | `engine/fx/FxEffect.cpp` | Strict parse and serialization of four shader-graph input names |
| `eb5debf6238a6e8709bb62a0a391d3d91708f759` | `engine/fx/FxInternal.h` | Cached `EmitterRT::graphInputSources` integer selectors, default `[0,1,2,3]` |
| `792b638bdae492fca338195b38cd81afa2545b0f` | `engine/fx/ParticleSystem.cpp` | Resolve four input names to numeric source codes ONCE while building the emitter |
| `30a8bc445b28f436b9ba39b6d24f7211cd332a75` | `engine/fx/FxRender.cpp` | Populate existing per-vertex `graphData` float4 from the selected VM / instance values |
| `a0d14421270339af58cbe01b2880e6ee29bfab8d` | `docs/spectrafx.md` | Public authoring, legacy defaults, material and CPU/GPU contracts |
| `2b13923319734f6dbd164c2c1185ef2b3f73e244` | `skills/visuals/aiverse-spectrafx/SKILL.md` | AI agent feature instructions, example inputs and safety |
| `a4e23970ce7c2df957d8eebb0fcf44750d64bcb4` | `AGENTS.md` | Engine-wide Stage 21 ownership and tests-deferred discipline |

### Data format, behavior and compatibility

`FxEffect::EmitterDef::graphInputs` is an optional fixed-width list of **exactly 4 strings** naming `age`, `lifetime`, `random`, `zero`, `attribute0..attribute7`, or `param0..param7`. `parseEffect` validates type/count/allowed names; `effectToJson` serializes the mapping without dropping authoring data. Values may be supplied through compiled v2 nodes such as `MaterialModule` or ordinary legacy `fx/<effect>.json`.

The CPU particle renderer previously populated the existing 40-byte `Vertex::graphData` with `[age,1/invLife,stableRandom(uid),0]`. The Stage-21 runtime caches each chosen source as a numeric code in `EmitterRT` and populates the existing four vertex channels without changing GPU vertex layout, texture atlas, material-key draw batching, shader pipeline descriptors, or renderer state. Existing shaderGraph material programs already receive this `float4` as `sg_particle`. Authors can write a bounded CPU `particleProgram` changing `attribute0..7` at Spawn/Update/Collision/Trigger/Death, or use Stage-20 named instance parameter values, and route selected values into the material graph.

**Default compatibility:** If `graphInputs` is absent, `EmitterRT` uses source codes `[0,1,2,3]` exactly matching the legacy vertex values. No changes are needed to existing content. The exposed `graphInputs` field is part of canonical effect serialization and source identity. Effects with nondefault input bindings remain on full CPU (strict `GraphRuntime` GPU eligibility uses whole-effect serialization) and are not silently simplified for Metal. No GPU code generation, custom shader JIT, new buffers, or game-specific ECS code is introduced.

### Actual verification

**NOT RUN (user explicitly deferred testing):** No CMake, compiler/clang, CTest, Metal GPU shader run, editor GUI interaction or screenshot comparison was executed for Stage 21. The GitHub connector confirmed source commit writes but that is **not** build/test success. Before production check exact shader `sg_particle` semantics, source-lane validation/roundtrip, instance defaults/updates affecting material per particle, native API integration, same-texture batching across different per-vertex mappings, and legacy visual parity.

### Known limitations and next FUNCTIONAL step

1. Custom 4-lane `graphInputs` forwards CPU VM outputs into existing shaderGraph. It is **not** an arbitrary material attribute stream beyond four floats. A shader graph wants more than four values must use additional designed vertex attributes or textures with explicit RHI/quality budgeting.
2. The limited Metal simple-billboard GPU path still does not execute the per-particle expression VM. It must continue to reject GPU-ineligible advanced graph effects, preserving full CPU behavior.
3. Full cross-platform GPU expression bytecode/kernel lowering and per-instance GPU parameter streams are unfinished. Any new GPU VM must have strict compile-time capability validation and a safe CPU fallback, and must not introduce genre dependencies or renderer state in authoritative ECS.
4. **Next bounded functional Stage 22:** implement an optional GPU-compatible expression subset for the Metal backend (read-only param inputs, finite arithmetic, supported spawn/update scalar targets) with conservative rejection for unsupported stages/nodes; avoid enabling by default. Continue native editor, runtime settings, material integration and final feature parity before running the user-deferred test suite.
5. Every new implementation stage must append a **separate handoff commit** to this living file, preserving other concurrent agents' entries and including exact SHA and what was/not actually tested.

## Stage 22 — opt-in Metal particle VM interpreter and per-particle parameter buffers (CODE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** move beyond Stage-21's CPU-only scalar particle VM by actually executing a strictly bounded Spawn/Update subset in Metal compute, retaining the existing 64-byte GPU particle state and preserving every unsupported effect on the complete CPU backend. The user explicitly requested functional work first and testing later. No FPS/RTS genre dependency, authoritative ECS writes or forced production GPU activation.

### Source commits and files (exact)

| Commit | File | Implementation |
|---|---|---|
| `9e9ce2a2ec0d383fe2d7e22a25fb3ec7cb7234b6` | `engine/fx/SpectraGpuProgram.h` | Added `gpu_program::prepareForMetal()`: strict executable budget/entire-program capability gate on the pre-existing fixed GPU payload |
| `662ef76397958c34800e8015fbbb6f7054ce41db` | `engine/fx/SpectraGpuShaders.h` | New MSL bounded stack interpreter (`vmRead/vmWrite/vmEvaluate/vmRun`) and Spawn/Update GPU hooks; program and param buffer bindings |
| `e72b3c99d2b726afc87bf5a729c084ef1dace73f` | `engine/fx/SpectraGpuParticles.h` | Opt-in `ComputeEmitterDesc::gpuProgram`, complete particleProgram JSON, typed parameter GPU buffers and `setProgramParameter` |
| `f3de3a5fdfb3a4c3f837ee4d867f3f1b4360eb30` | `engine/fx/SpectraGpuParticles.cpp` | Strict Metal VM eligibility at init; reject GPU when disabled/incompatible; preserve complete VM in CPU fallback |
| `db90ddd3e514525be38689c9a4f117c97e355331` | `engine/fx/SpectraGpuParticles.cpp` | Persistent buffers for the 10,256-byte program ABI and 32-byte per-slot seed/live program parameters; allocation, disposal, resource budgets and clear path |
| `f477ff9f9a16cb75bf84fb4319b9295a692c30d5` | `engine/fx/SpectraGpuParticles.cpp` | Snapshot params on birth; upload param state before compute; RHI slots 7,9,10; pass through bounded VM on Spawn/Update; existing-parameter updates and CPU fallback parity hooks |
| `a70f8787772d23ea5d90a2888f66da9ac58c1413` | `engine/fx/SpectraGraphRuntime.h` | Public `GraphRuntimeOptions::gpuProgram` default-off opt-in |
| `311cd845e01dac2df6b579910f2a32f3879f3cc4` | `engine/fx/SpectraGraphRuntime.cpp` | Whole-effect GPU VM source validation/dispatch, program serialization equality, per-source named parameter forwarding, CPU fallback |
| `03cff21fe53f370cc08da4c2b45ddb46551bcd64` | `engine/fx/SpectraGraphRuntime.cpp` | Correctly handle named schemas without a GPU-executable particle program |
| `de15d2424eb7c5c3f4a815b6a772eb4e79e36f47` | `examples/spectrafx/gpu_vm_lift_v2.vfx.json` | Example: v2 named lift parameter, runtime age expression, per-particle Spawn/Update rise and fade |
| `9da635ad1f241c8d3b3472dde00f1d3ac71ac264` | `engine/fx/SpectraGpuProgram.h` | Compile-time ABI assertions for fixed enum values and payload sizes |
| `de86610baccd2eba208fb6cee9becc9344ca7489` | `engine/fx/SpectraGpuParticles.cpp` | Cleanup tracked CPU fallback handles during clear |
| `260f4fee6c131ffa433844f9c4a6d8f199fc5042` | `apps/editor/core/SpectraGraphOps.cpp` | MCP compile diagnostics: `metalVmEligible`, `metalVmReason` separately from legacy GPU compatibility |
| `f2f9581fc8ff8b883fe0e16ede03886ed388bcfb` | `apps/editor/windows/SpectraGraphWindow.cpp` | Graph editor shows opt-in GPU VM compatibility and CPU fallback reason |
| `c7af921aac784af9b8a3712c0e18b0c75619157d` | `docs/spectrafx.md` | Public Stage-22 implementation/API/platform/safety/validation documentation |
| `d4a772c661807788fcfdf8f77806dbf693121627` | `skills/visuals/aiverse-spectrafx/SKILL.md` | AI agent usage instructions and strict GPU subset |
| `a4cfaa43ac22948281cf9b3dd989e209715634ee` | `AGENTS.md` | Multi-agent handoff and tests-deferred safety contract |

**Concurrent work:** another agent previously added `SpectraGpuProgram.h` with `gpu_program::Payload` and a nonexecuting `prepare()` stage. Stage 22 **extends** that existing ABI; do not duplicate its compiler or misstate the original payload definition as newly introduced by this stage.

### Public API, CPU/GPU semantics and compatibility

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntimeOptions opts;
opts.preference = aiverse::spectra::ComputePreference::Automatic;
opts.gpuProgram = true;              // experimental, default FALSE
opts.gpuIndirect = false;            // independent opt-in from Stage 14

aiverse::spectra::GraphRuntime fx;
if (!fx.initGraph(device, graphJson, aiverse::Json::object(), opts)) {
    // fx.reason() / fx.errors()
}
fx.setNamedProgramParameter("lift",3.0f);       // future births
fx.burstAt({0,2,0},{0,1,0});
fx.setNamedProgramParameter("lift",5.0f,true);  // also live particles
// Per presentation frame: Device beginFrame; fx.step before scene render pass;
// fx.draw inside scene pass; endPass/endFrame. Release before device teardown.
```

The existing Stage-22 CPU GPU-payload gate now validates the entire `particleProgram` and rejects unsupported stages. Executable MSL reads a bounded, fixed-layout `gpu_program::Payload` stored on GPU, without shader code generation or CPU JSON interpretation at every particle update. Only Spawn/Update stages with <=24 statements, <=192 total instructions and <=48 instructions per statement are accepted. Supported reads: age, ageNormalized, dt, world xyz position, xyz velocity, size, tint rgba, and param0..7. Supported writes: position xyz, velocity xyz, size, tint rgba. Supported ops: numeric constants, field read, add/subtract/multiply, min/max, abs/negate/saturate, clamp, lerp. Supported actions: set, add, addRate, multiply. Nonfinite / out-of-range intermediate and output values are skipped, size/tint remain bounded.

Unsupported: custom attribute0..7 registers, rotation, uid, death/collision/trigger program stages, comparisons, conditional Select, division/pow/sqrt/trigonometric expressions, any other shaderGraph/material plugins, particle events, force fields, multiple emitters, ribbons/meshes, arbitrary graph stages. The normal strict whole-effect JSON equality gate remains active; a program alone does **not** grant eligibility. `RequireGpu` refuses semantic loss; `Automatic`/`PreferGpu` retain the complete original CPU effect. Legacy Graph v1 and ordinary effects remain unchanged. `gpuProgram` stays disabled unless native caller explicitly enables it.

On Metal, the VM is fixed ABI version 1 and uses extra RHI storage buffer slots: **7** = spawn-time 32-byte `VmParams` (two float4), **9** = persistent per-physical-particle 32-byte `VmParams` state, **10** = validated 10,256-byte program payload; **8** remains the existing per-frame uniforms. Per-particle GPU position/velocity remain in the original 64-byte particle state; vertex layout remains 40 bytes. GPU snapshot params are captured before Spawn and carried into every Update. For `affectExisting=true`, presentation CPU tracks only lifespan/slot identity, modifies its parameter snapshots and uploads state to GPU at the next safe compute step; it does **not** read positions from the GPU. Both Stage 13 host-gather and Stage 14 GPU-indirect draw paths use the same Spawn/Update VM.

`GraphRuntime::initGraph` allows named v2 `instanceParameters` on GPU only if `gpuProgram` is enabled AND an actual particleProgram exists; otherwise it explicitly selects full CPU or rejects `RequireGpu`. Raw and named parameter setters route to the actual CPU or Metal wrapper, without changing authoritative ECS data. The MCP/editor `metalVmEligible` and `metalVmReason` fields diagnose **asset compatibility only**, not real hardware availability. Editor preview remains CPU.

### Verification status — tests explicitly deferred

**NOT RUN:** no local build, CTest, Metal MSL shader compiler/real-device render, shader readback, editor GUI interaction, benchmark or native genre-client integration test was run in this stage, following the user's explicit instruction to finish functions first. GitHub file updates and SHA observations were performed. Compile-time ABI assertions are **written**, not compiler-confirmed. No test outcome may be claimed PASS. The prior handoff documented queued/cancelled CI for previous feature commits; do not reuse old statuses as Stage-22 validation evidence.

Later validation commands:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_graph_tests aiverse_vfx_shared_batch_tests aiverse_vfx_cue_scheduler_tests aiverse_editor aiverse_vfx_metal_smoke -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

On Apple Silicon specifically compare GPU/CPU Spawn+Update math, per-instance default/change, >1 independent source with same graph but different `lift`, direct versus indirect draw, shader payload layout/slot limits, full CPU fallback for forbidden ops, clear/reinit/device-loss and MSL compile errors. Existing macOS smoke has no GPU VM cases yet; extend it when tests are scheduled, and add numerical/visual parity screenshots and GPU timings.

### Known limitations and next FUNCTIONAL stage

1. Stage-22 GPU VM is **experimental source code, not validated** on a real Metal device. Large fixed payload and two 32-byte-per-slot buffers increase GPU memory/bandwidth compared with the earlier simple billboard path; evaluate performance during the postponed QA phase before enabling in production.
2. The GPU interpreter does not store `attribute0..7`, render arbitrary CPU custom graphInputs/material shader graphs, execute collision/death/trigger event programs, or handle GPU-born subemitters. Unsupported effects MUST retain full CPU.
3. Only Metal has this GPU VM implementation. The checked-in future GLSL compute source does NOT yet implement the VM; Vulkan/GL/GLES/WebGL fallback to CPU. Preserve this contract.
4. Multiple advanced effects still use separate `ComputeParticles` resources. A genre-neutral GPU shared emitter pool, richer simulation/render modules, cross-material sorting/soft particles and native production integrations remain functional work.
5. **Next bounded Stage 23:** add a GPU custom particle attribute storage extension plus input/output mappings for selected VM variables, then expand Metal GPU eligibility one feature at a time while retaining strict CPU fallback. Alternatively, implement multi-emitter GPU pooling and editor authoring of performance tiers. Keep it opt-in. The user wants **functions first; tests later**. Append a separate handoff commit after every next implementation stage in this same file.

## Stage 26 — persistent Metal VM custom particle attributes (SOURCE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** finish the next bounded *functional* SpectraFX increment: persistent eight-channel per-particle user float attributes in the opt-in integrated Metal Spawn/Update VM, matching the existing CPU VM register semantics. Preserve all renderer/ECS/genre-neutral boundaries and exact original CPU fallback for unsupported features. The user explicitly asked to finish functions before running tests.

**Concurrent work:** Earlier GPU VM milestone files are already labeled Stage 23 (standalone Metal VM pass), Stage 24 (generation-safe source parameter snapshots), and Stage 25 (newborn/partial-upload controls); they belong to other agents and have different kernels/runtime contracts. Our integration is **Stage 26**, not a duplicate of those milestones. The existing living handoff through Stage 22 has been preserved; no other agent's code was overwritten. Feature remains opt-in.

### Exact implementation/source commits

| SHA | Path | Implementation |
|---|---|---|
| `ba2bdf3bec40814d66ece643479e2b698be11ba6` | `engine/fx/SpectraGpuProgram.h` | GPU VM eligibility now accepts `program::Field::Attribute0..7` for expression reads and assignment targets, retaining bounded op/stage policy |
| `5a6f55e1dd5a9e29bc882d62260b8468ae552d34` | `engine/fx/SpectraGpuShaders.h` | Metal compute slot 11 `VmAttributes` (two float4), dynamic VM attribute reads and writes, fresh zeroed Spawn state, persistent Update state |
| `8a166df5da816707abe5fcaa9f033b750d58768e` | `engine/fx/SpectraGpuParticles.h` | C++ 32-byte `ProgramAttributes` storage type, size assertion and resource handle |
| `2f7dc457ea06848f1d7a83c46adda99ceebc1357` | `engine/fx/SpectraGpuParticles.cpp` | Allocate/initialize, bind, clear and destroy GPU attribute buffer; account for extra GPU memory |
| `d385cd1148f8db89b0f8b370f57161751988ac80` | `engine/fx/SpectraGpuProgram.h` | Explicit ABI field-ID assertions for attributes 16..23 and params 24..31 |
| `eac63124eb60d97251a3c05f78cea539c4e8519f` | `examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json` | Complete v2 additive billboard sample with named `intensity`, `attribute0` Spawn write and Update-driven tint alpha/size |
| `1776d4fe2d18c9d81faa7d4fccc790828d78f4f9` | `engine/fx/SpectraGpuProgram.h` | Clarify that ABI source is opt-in executable via integrated Metal backend, not GPU validation proof |
| `10d9d4a68ca5f9cc191139ccf76477459c7e31b8` | `engine/fx/SpectraGpuVmDispatch.h` | Safeguard standalone Metal VM utility: reject custom attributes because its separate shader has no persistent register buffer |
| `917ef6fe19c7ed720c8b691bb79cb5e216892dd1`, `3b23167055727661a324a2b9f6f97af7de4deabc` | `docs/spectrafx.md` | CPU/GPU attribute semantics and Stage-26 numbering |
| `a52ae7ca4b02c15f9bf4ce6b5b14cc69e65a2c7c`, `0c79d69863cb30c5a8ebbf36109b1296d5f4b07d` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent integration guidance, strict CPU material fallback and stage numbering |
| `19f34a3fb7d60b45652ca73e5f5e8b0470bdb4a9`, `0e84f29229758dd5ad09dd81b33d06837b512ade` | `AGENTS.md` | Stage 26 independent GPU attribute ownership, user-deferred testing and per-stage handoff rule |

### Source/compute ABI and exact lifecycle

1. `fx::SpectraParticleProgram` already supports writable scalar `attribute0..7` fields on the existing CPU renderer. The Stage-22 integrated GPU payload uses matching uint32 field IDs **16..23**; these are now approved by the conservative GPU VM compiler for both reads and writes. No new VM opcode or arbitrary external executable source is introduced.
2. `ComputeParticles::ParticleState` remains exactly **64 bytes** and its expanded billboard vertex remains **40 bytes**. A separate **32-byte-per-slot** `ProgramAttributes` / MSL `VmAttributes { float4 a0,a1; }` buffer holds eight floats per stable physical particle slot. New compute resource slot **11** binds that buffer. This works with existing compute buffers 0..10 and the previous GPU indirect/host-gather paths, without changing RHI device signatures or index-buffer limits.
3. Initial GPU allocation creates zeroed attribute state for all capacity slots. On every GPU Spawn, a newly allocated/reused slot starts with **zeroed** registers. Spawn VM expressions may write registers in sequence, and the finalized record is persisted. On every subsequent Update, the shader loads this slot's record, executes bounded VM code in order, then stores it back. VM statements can read attributes modified by earlier statements in the same stage.
4. `ComputeParticles::clear()` zeroes GPU state and attributes only outside active render/compute encoders (existing RHI restriction); `releaseGpu()` destroys the attribute buffer, and `gpuBytes` includes its capacity. CPU bookkeeping remains outside authoritative game state. There is no readback of GPU particle attributes to CPU each frame.
5. The version-2 sample `examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json` captures the named runtime instance parameter `intensity` into `attribute0` at Spawn and later reads `attribute0` on Update to set tint-alpha/size. `GraphRuntimeOptions::gpuProgram` remains **false by default**; only explicit opt-in and full whole-effect eligibility can attempt Metal. All other effects retain the full `ParticleSystem` implementation.
6. **Important independent utility boundary:** `gpu_program::MetalVmPass` uses a different MSL shader and its inputs/outputs don't have custom-attribute registers. Its initializer now rejects attribute writes/reads rather than claiming capability incorrectly. Its other features from Stages 23-25 remain untouched.

**Out of scope:** GPU arbitrary `shaderGraph`, Stage-21 `graphInputs` custom material bindings, custom animation rotations, `uid` fields, collision/trigger/death stages, sub-emitter callbacks, mesh/ribbon renderers, GPU lifetime allocator, Vulkan/GL/GLES/WebGL GPU VM. Persistent attributes may still affect visible GPU sprite output through VM writes to tint, size, position or velocity. No silent GPU downgrade of authored data is permitted; `RequireGpu` must remain strict.

### Actual checks performed

**GitHub source inspection and commits only; NOT RUN:** The user explicitly postponed tests until all desired functions are implemented. No native CMake configure/build, CTest, Visual Studio/Clang verification, Metal shader compiler, GPU compute dispatch/readback, FPS/RTS sample screenshot, perf benchmark, or CI-passing validation was run by this agent. Static ABI assertions are authored but **not** compiler-verified. Source commits are not evidence of tested runtime correctness.

For the eventual deferred validation phase, run `./run-tests.sh` per AGENTS.md; specifically include SpectraFX graph/VM compiler checks, headless GPU command protocol, actual Apple Silicon MSL `cs_main` compile/simulation, per-particle attr persistence across Update and slot reuse, CPU vs GPU visual/numeric parity and GPU fallback for unsupported event/material stages. Record actual PASS/FAIL/SKIP and exact commit SHA only after executing those checks.

### Limits and next FUNCTIONAL implementation

- Stage 26 enables persistent GPU VM attributes in the **integrated Metal sprite emitter** but not generic GPU shader-graph material effects. Material binding of the CPU `graphInputs` channels to a GPU shader pipeline remains a separate capability that must be explicitly supported rather than ignored.
- The standalone `MetalVmPass` still lacks attribute storage; it now fails explicitly if given a program containing those fields. Adding a caller-owned attribute buffer and matching standalone shader would be a separate source-compatible extension.
- The fixed GPU particle pool remains per `ComputeParticles`, and CPU still owns birth/death slot bookkeeping; no cross-emitter GPU pool, global alpha-sorted transparency or true GPU-driven lifecycle.
- **Next Stage 27:** implement a restricted GPU material output path for selected `attribute/param` values using the already available 40-byte vertex graph data, with strict output/source validation and fallback for unsupported shader graphs, then continue shared multi-emitter GPU resource pooling and scalability controls. Maintain genre independence and separate handoffs. Tests remain deferred until the user changes that instruction.

## Stage 27 — restricted GPU particle shader graph materials (SOURCE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/universal-entity-inspector-fps`.
**User instruction:** finish functional SpectraFX work before running tests.
**Goal:** allow opt-in **Metal GPU sprite materials** to consume custom particle `attribute0..7` and per-source `param0..7` values through the existing `graphInputs`/particle-domain `shaderGraph` format, without implementing a second shader graph language or silently dropping unsupported authoring. Preserve the full CPU effect and its renderer as the default/fallback. All gameplay/ECS code remains unmodified.

### Exact source and documentation commits

| SHA | Path | What changed |
|---|---|---|
| `2adc7b398439fc84718a975537edd3a4a8f0500e` | `engine/fx/SpectraGpuMaterial.h` | New reusable strict capability validator for GPU particle shaderGraph and trusted-template GLSL/MSL shader builder using the existing `render::graph::compile` program |
| `298fc9a4e6dc56629476c6f45f44dcaa35fbd9ae` | `engine/fx/SpectraGpuMaterial.h` | Use repository-supported JSON bounded-array size check |
| `0ba6c1e33fbfbf312719b3ad08caadd51496c767` | `engine/fx/SpectraGpuMaterial.h` | Explicit required C++ utility include |
| `54866e94b3590d578da8d6e233e104961ef87b40` | `engine/fx/SpectraGpuParticles.h` | `ComputeEmitterDesc::gpuMaterial` opt-in, preserved `shaderGraph`/`graphInputs` and GPU graph input source handle |
| `91d1c6b21e811a431539880ed59ba06251c87abc` | `engine/fx/SpectraGpuShaders.h` | Stage-26 integrated Metal kernel gathers four graph source floats from GPU attributes/param snapshots/age/lifetime into 40-byte vertex graphData; new compute storage slot 13 |
| `8ba1abe15d1403936af41a238486c80af37685f8` | `engine/fx/SpectraGpuParticles.cpp` | Validate material opt-in/direct config; preserve full shaderGraph and graphInputs in direct CPU fallback; allocate/bind/release 16-byte source map; compile GPU material shader and select four-attribute graphics pipeline; old no-material pipeline unchanged |
| `f172bfd386fc5c52761434374e1fdd48020faa99` | `engine/fx/SpectraGraphRuntime.h` | Add default-false `GraphRuntimeOptions::gpuMaterial` |
| `d25de54766c4da5d117a9e91346745a851efecba` | `engine/fx/SpectraGraphRuntime.cpp` | Strict whole-effect eligibility for GPU material subset; preserve material fields in normalized definition equality; populate GPU emitter config; reject unapproved material GPU with reason |
| `9d08f7f6b719294e1c648c103e0f7252ac5b7188` | `examples/spectrafx/gpu_vm_material_inputs_v2.vfx.json` | Complete v2 graph: runtime intensity -> custom VM attribute0 -> particle material `particleAge` lane -> multiply base sprite tint |
| `e7b01c9a2815ef52a994fbe5be2995c0e9f74f28` | `docs/spectrafx.md` | Public stage-27 format, opt-in API, GPU/CPU and platform limits |
| `10ac5097f59810c8ae1a7760a63f2146bc648e23` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent authoring and strict shader/material opt-in instructions |
| `1a3963013d5751017562e08cf3f8bfc8772fe4ce` | `AGENTS.md` | Stage-27 ownership and deferred-test rules |

### Native API, data/compute/pipeline layout and CPU parity

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::GraphRuntimeOptions options;
options.preference = aiverse::spectra::ComputePreference::Automatic;
options.gpuProgram = true;  // if ParticleProgramModule uses custom registers
options.gpuMaterial = true; // a bounded particle shaderGraph
// options.gpuIndirect = true; // separate experimental capability, not required
aiverse::spectra::GraphRuntime runtime;
runtime.initGraph(device, graphV2, aiverse::Json::object(), options);
runtime.setNamedProgramParameter("intensity",0.8f);
runtime.burstAt({0,1,0});
// Device beginFrame + runtime.step() before scene pass;
// runtime.draw() inside scene render pass.
```

1. `gpu_material::compile(shaderGraph,graphInputs,...)` first checks the **full authored material graph**, then delegates to the existing trusted `render::graph::compile(graph,"particle")`, which already generates typed MSL/GLSL statements. This class does **not** accept arbitrary authored shader snippets. Its GPU subset is limited to particle-domain graph v1 with at most **32 fragment-only nodes**, only a `color` output, no vertex material graph, no external textures, and only builtin sample/UV/input/vertexColor, particle channels and bounded colour/math ops. CPU supports much more; ineligible graphs stay on CPU. No genre-specific conditions.
2. The existing CPU FX asset field `graphInputs` is reused. GPU supports `age`, `lifetime`, `zero`, `attribute0..7`, `param0..7`. CPU UID-based stable `random` is **not GPU-compatible**, so the GPU selector rejects it rather than silently changing behaviour. Similarly a material graph reading the default `particleRandom` lane requires an explicit, compatible `graphInputs` mapping. The four names are decoded once to four `uint32_t` source selectors and stored in a **16-byte** GPU resource (compute storage buffer **13**).
3. During Metal quad expansion (both Stage-13 host-index gather and Stage-14 GPU atomic compaction), the shader reads the physical slot's `ParticleState`, `VmParams` and Stage-26 persistent `VmAttributes`; writes selected values into the four existing `Vertex::graphData` floats at offset 24. The sprite's particle state remains **64 bytes**, expanded vertex **40 bytes**, and previous VM/event/lifetime/resource-buffer slots 0..12 are unchanged. No GPU readback or extra vertex stride.
4. Only for a validated material graph, the Metal pipeline adds `Float4` vertex attribute **3** at byte offset 24 and uses a trusted generated graphics shader with `sg_particle` source lanes and the compiled graph's `color` output. Other GPU billboard effects keep the preexisting three-attribute default pipeline; shader/material programs are still compiled once on initialization, not during each frame.
5. `GraphRuntimeOptions::gpuMaterial` / `ComputeEmitterDesc::gpuMaterial` default **false**. ShaderGraph presence without this flag prevents GPU routing. The full normalized `EffectDef` check includes `shaderGraph`, `graphInputs` and existing `particleProgram` only after each capability gate has accepted them; every other unsupported emitter parameter, stage, module or material feature rejects GPU. `ComputePreference::RequireGpu` returns an error; `Automatic` or `PreferGpu` keeps the full original CPU effect.
6. Direct `ComputeParticles` CPU fallback copies both graph fields to its `EmitterDef`, in addition to its previously preserved particleProgram, avoiding loss of material authoring. Metal custom material graph rendering is still a single additive `builtin:soft` camera-facing sprite emitter, not full multi-emitter material support.

### Verification actually performed

**SOURCE INSPECTION AND GITHUB WRITES ONLY; ALL TESTS DEFERRED / NOT RUN.** The user explicitly deferred testing until completion of functional work. No CMake/clang build, CTest, live MSL shader compilation, Apple Silicon GPU execution, image capture, CPU/GPU visual or numerical parity, editor GUI interaction, performance/memory profiling or CI conclusion was observed for this stage. The connector confirmed file changes and commit SHA, not runtime validity. Do not claim a proven or shipping-ready GPU material pipeline.

During the eventual test phase, run the unified `./run-tests.sh` per AGENTS.md. Verify: invalid/missing external textures/material nodes fail GPU eligibility without loss; `RequireGpu` refuses; attributes persist through Spawn/Update and recycled physical slots; named parameters yield independent colours for two runtime instances; source mapping/UV/HDR/fog parity in actual Metal screenshots; direct/indirect GPU draw; source buffer at compute slot 13; shader compilation in Metal and GLSL fallback; shared-BatchWorld CPU shader rendering is unaffected. Check API/ABI against actual RHI.

### Known limitations / exact next functional stage

- GPU graph subset is **fragment-only** with builtin soft-sprite texture. Mesh, ribbons, scene depth collisions, arbitrary external texture assets, vertex displacement, alpha sorting and general shader/material plugins remain CPU-only; camera-facing additive blending is unchanged.
- The Stage-26 eight-register GPU VM is restricted to supported Spawn/Update scalar operations, not event stages, rotations, arbitrary CPU physics, module forces or sub-emitters. Strict full CPU fallback is mandatory.
- Every `ComputeParticles` still owns separate GPU state/vertex/pipeline resources. CPU still owns physical birth/death bookkeeping, and `gpuIndirect` remains an independent opt-in path. No multi-source shared GPU allocator or global GPU material batching exists yet.
- **Next bounded Stage 28:** build a genre-independent shared GPU emitter resource/pool scheduler with explicit per-emitter material keys, capacity/quality budgets, lifecycle and source reset semantics; preserve the existing shared `BatchWorld` CPU path and never silently switch existing FPS/RTS clients. Provide separate handoff with exact SHAs. Continue postponing tests until user requests them.

## Stage 28 — pooled GPU resource groups and shared CPU fallback (CODE COMMITTED; TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** allow many independent presentation-only VFX sources to share Metal emitter simulation/storage and draw resources when their **whole effect graph** and actual GPU policy are identical, while keeping the mature cross-genre CPU BatchWorld fallback. Do not silently switch FPS/RTS production clients. User continues to request **functional implementation before tests**.

### Exact source commits

| Commit SHA | File | Change |
|---|---|---|
| `f32a90b04e434ec74179bd73a1803adfccd5f20f` | `engine/fx/SpectraGpuParticles.cpp` | Allow valid per-instance GPU parameter snapshot updates for material-only shaderGraph sources without a separate ParticleProgramModule |
| `1f0bb9c434d75fee92ef9ac4ea694e301ae91487` | `engine/fx/SpectraGraphRuntime.cpp` | Permit named runtime parameters on GPU when either a supported VM or supported GPU material can consume them |
| `3744648ddecf38e10aa62fadb5fbc2ac23137405` | `engine/fx/SpectraGpuPoolWorld.h` | Public `GpuPoolWorld`, `PoolWorldBudgets`, `PoolWorldStats`, per-source material/VM parameter input, lifecycle and rollback API |
| `ac0afeca25f507be10947ed35322978130ed1aca` | `engine/fx/SpectraGpuPoolWorld.cpp` | Group GPU emitters by full serialized effect and policy, reuse group resources, throttle births, submit per-source values, batch full CPU fallback and schedule per-frame update/draw |
| `b6543f84bf9bd9a1d18289240975497dee7ee35d` | `engine/fx/SpectraGpuPoolWorld.h` | Private unique CPU definition/GPU group count recalculation |
| `b5acc48c83d3dcd7c5da3f1d44ce44985ce5f482` | `engine/fx/SpectraGpuPoolWorld.cpp` | Count deduplicated CPU definitions correctly after name changes; reject per-burst values violating named parameter bounds; report actual live GPU availability |
| `bd94a286cb23e0afdc728b4ca9a45c0a6d196ef4` | `engine/fx/SpectraGpuPoolWorld.cpp` | Length-prefix group identity and require healthy RHI for visual rollback reset |
| `dafab77cb6ee9d6598d5d87519afffd35cffa6dc` | `engine/fx/SpectraFX.h` | Export shared pool from canonical native SpectraFX public header |
| `34d706061ddd6eb98284726be44984c77f747d53` | `CMakeLists.txt` | Build GPU pool implementation into `aiverse_fx` / `AIVERSE::spectrafx` |
| `92c1a3f83747db550f46a2eae03ed8d20e616ab7` | `docs/spectrafx.md` | Public usage, per-effect resource grouping, CPU fallback, budgets, frame semantics and limits |
| `e2dd8251d726084964d5945a02625d1e9a85cd2c` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Native agent usage and exact unsupported features |
| `7aeccc7dc018f1e7119d7d6a0016350186f5ff12` | `AGENTS.md` | Required engineering contract, per-frame graphics lifecycle and deferred validation |

### Native API / architecture

`GpuPoolWorld::init(gfx::Device*,PoolWorldBudgets)` creates **one owned CPU `BatchWorld`**, a table of independent logical effect bindings and lazily-created unique GPU groups. Use `defineGraph(name,graph,overrides,GraphRuntimeOptions)` or `defineEffect(name,completeEffect,options)`. On eligible Metal devices, a GPU group is shared whenever the complete `fx::effectToJson(effect)`, actual capacity, quality and `gpuProgram`, `gpuMaterial`, `gpuIndirect` selections match. Resource keys are length-prefixed to avoid ambiguous concatenation. A group owns one persistent `ComputeParticles` with VM payload, physical particle pool, material pipeline/texture and CPU-owned slot bookkeeping; two logical source names with the same effect share this exact allocation. Different effect definitions remain isolated even if they use identical textures.

The world clamps GPU-group count to 64, capacity per group to 4096 physical slots and births to 4096 per presentation frame, with caller-configurable tighter defaults. If the GPU group limit, hardware capability or material/VM eligibility prevents a GPU group, `Automatic`/`PreferGpu` retain the original **full** effect definition in the single owned `BatchWorld`, which already batches compatible CPU effects by material. `RequireGpu` fails with `reason()` instead of creating a fake GPU group.

An individual binding stores independent v2 `instanceParameters` default values and validates updates through `setNamedParameter(sourceName,paramName,value)`. During `burst(name,position,direction,tint,sourceValues?)`, the GPU group sets only its **future** `param0..7` values and snapshots them onto the born particles, so previous particles of other sources keep their old VM/material inputs. CPU births use `BatchWorld::playWithProgramParameters` for prewarm-safe parameter capture. Stage 28 corrects the integrated `ComputeParticles::setProgramParameter` guard to accept a supported shaderGraph material that reads params with no VM module, and `GraphRuntime::initGraph` allows such schemas on GPU when the material opt-in gate is satisfied.

A typical host sequence is:

```cpp
spectra::GpuPoolWorld world;
world.init(device, budgets);
world.defineGraph("engine_left", graphV2, Json::object(), options);
world.defineGraph("engine_right", graphV2, Json::object(), options); // SAME GPU group
world.setNamedParameter("engine_left","intensity",0.35f);
world.setNamedParameter("engine_right","intensity",0.90f);

// After Device::beginFrame and before any render pass:
world.beginPresentationFrame(camera,viewProj,width,height);
world.burst("engine_left",{0,1,0});
world.burst("engine_right",{3,1,0});
world.step(dt); // once per unique GPU group; once for shared CPU world
// Inside an already-open scene render pass:
world.draw(fog);
// After the pass:
world.endPresentationFrame();
// For a new match/visual rollback (between frames only):
world.resetForRollback();
world.shutdown(); // before Device destruction
```

`PoolWorldStats` reports unique GPU groups/CPU definitions, submitted/rejected one-shot bursts, alive particle estimates, GPU memory and actual draw submissions. `isGpuEffect(name)` checks the group's real Metal-ready status, not only its nominal registered preference.

### Explicit limitations

- This is **resource grouping for IDENTICAL FULL EFFECTS**, not a single physical GPU allocator for all materials or one draw across different material graphs. Unique effects still have their own GPU buffers, shaders and indirect counters.
- The public pool handles **one-shot births**. It does not expose generation-safe per-source GPU stop/kill/move handles or continuous ECS-attached GPU emitter simulation; retain the separate mature shared CPU `CueScheduler` for attached gameplay visuals. `resetForRollback()` resets **every GPU/CPU visual belonging to this pool**, but not arbitrary external renderer owners. The graphics device must be healthy, and reset cannot run during an active pool frame or GPU encoder.
- Group resources are retained/cached for reuse until world shutdown; redefining names does not destructively free possibly still-live groups. Hot-reload-heavy tooling can reconstruct the world at a safe scene boundary if GPU-group capacity is exhausted.
- The advanced CPU module system (trails/meshes, external material textures, sorting, collision/trigger/death VM events, etc.) remains complete **CPU fallback**. Metal only supports the explicitly selected Stage-22/26/27 simple additive sprite subset. Vulkan/GL/GLES/WebGL still do not use compute from this interface. No authoritative ECS state or FPS/RTS genre module dependency was introduced.

### Verification actually performed

**NOT RUN — as requested by the user.** Stage-28 C++ code, CMake source registration, public header and docs were committed through GitHub connector writes, but no local AIVERSE checkout build, Clang/MSVC compile, CMake configure, CTest, Apple MSL shader execution, screenshot comparison, GPU profiling, memory leak detection, ECS rollback replay or CI success was observed. Therefore this is a functional source implementation, **not verified to compile or ship**.

During the later requested test phase, run the repository's unified `./run-tests.sh` per `AGENTS.md`. Validate distinct binding names sharing one live GPU allocation; mixing CPU-only effects and compatible GPU groups; multiple burst calls and independent per-source custom VM attributes; capped group count/capacity; RequireGpu failure; reset between frames (and refusal during active encoders); long-running hot reload; cross-platform fallback; actual Metal material draw-count parity, cleanup/device-loss behavior, RAM/GPU bytes and startup complexity.

### Next bounded FUNCTIONAL work

1. **Stage 29:** add generation-safe logical GPU source handles, selective stop/kill/restart semantics for attached ECS emitters, and presentation rollback checkpointing without clearing unrelated sources sharing the same GPU group. This requires tracking source ownership of individual physical particle slots and resetting/releasing individual slots only when GPU render commands have finished, rather than just naming aliases of the same group.
2. Extend group-level LOD/culling and pooled GPU quality budgets (distance, importance, screen-size, memory/fill targets), then integrate with Terra/PHYSIS/Water via **genre-neutral visual event adapters**, not by hardcoding FPS/RTS rules.
3. Verify the actual Metal shader/material/compiler and all previous stages after functional completion, as the user explicitly postponed tests. Keep the next stage handoff separate with exact SHA and truthfully recorded verification results.

## Stage 29 — shared GPU-pool committed-tick VFX bridge (SOURCE COMMITTED, TESTS DEFERRED)

**Date:** 2026-11-19. **Branch:** `feature/universal-entity-inspector-fps`.
**Goal:** connect any genre's committed gameplay events to the now-available shared `GpuPoolWorld`, with bounded one-shot queues, tick de-duplication and explicit rollback reset. Reuse already-implemented Stage-27 GPU fragment shader materials and Stage-28 shared GPU grouping rather than rewriting them. Do not mutate gameplay/ECS state or switch existing native client renderers automatically.

### Exact implementation commits

| SHA | Changed file | Implementation |
|---|---|---|
| `17e2d624513012e05c80034e9174adae5d93f5b7` | `engine/fx/SpectraGpuCueBridge.h` | `GpuCueBridge`, `GpuCueBudgets`, `GpuCueEvent`, `GpuCueStats` for generic committed one-shot events and queue |
| `71c0f3feb55e22476e5bbb5e27666d5a42116aa1` | `engine/fx/SpectraGpuCueBridge.cpp` | Bounded per-tick admissions/frame flush, no duplicate tick replay, event name binding snapshots, strict pool-level rejected launch telemetry, rollback |
| `369b91665e143eba5974d9c38fa609184cc07de1` | `CMakeLists.txt` | Compile bridge into existing `aiverse_fx` / `AIVERSE::spectrafx` target |
| `c8974c3b40f99572b9513881826834cf698d17a1` | `engine/fx/SpectraFX.h` | Export `SpectraGpuCueBridge.h` from canonical native engine SpectraFX entry |
| `d0fe1e21a05ec8fa053c4ae3047775d3d15ec2ac` | `libraries/include/aiverselib/genre/SpectraGpuCueEcsAdapter.h` | Read-only `acceptSimGpuCueTick(bridge,tick,span<SimEvent>)` bridge without genre-specific logic |
| `aea65019e48dd8e7e3a8865a9d52f59c48a2bc58` | `libraries/include/aiverselib/genre/SpectraFX.h` | Export ECS one-shot adapter to native game modules |
| `1b6f27a88701f270c07b961bbc08119dddb773bc` | `docs/spectrafx.md` | Stage-29 native usage, queue/rollback contract and explicit limitations |
| `d9d9bdbf939585f36a3f63f559ace9a1d96d357d` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent usage and user-deferred verification instructions |
| `d7b15e28133d4d59520c73dae97552022f02e4f7` | `AGENTS.md` | Owner, ECS purity, one-shot limitation and per-stage handoff requirement |

### Architecture / API and runtime lifecycle

`GpuCueBridge(GpuPoolWorld&, GpuCueBudgets)` does not allocate a new `ParticleSystem`, own GPU state, or create another materials system. It references the caller-owned `GpuPoolWorld` which already groups compatible GPU graph definitions and preserves complete unsupported effects through its shared CPU `BatchWorld`. The pool **must outlive** the bridge and the graphics `Device` must outlive the pool.

`bind(gameEventKind,registeredEffectName)` is a purely presentation-level map. `accept(tick,span<GpuCueEvent>)` accepts only monotonically increasing **committed** ticks; duplicate or decreased ticks are ignored until a rollback reset. Every accepted event captures its current effect name, so rebinding a kind later doesn't change queued work. A cue optionally carries a generation-safe `source` ID, tint, direction and eight per-burst scalar VM values. The queue is bounded (`queued`, `acceptedPerTick`, `dispatchedPerFrame`) and the underlying pool has an independent global `maxBurstsPerFrame` budget. `flush()` sends bounded queued events only between pool `beginPresentationFrame` and `step`. World failures are counted and dropped, not declared successfully emitted.

`resetForRollback()` calls the pooled world's **full visual reset first** and clears bridge queue/tick counters only on success; if reset is unsafe inside an active RHI pass, error is propagated and the bridge is left intact so the host may retry safely. **Full-pool ownership is required**: unlike the CPU CueScheduler which can kill only its own handles, this GPU bridge resets all effects of the shared pool. Don't use independent rollback owners in the same pool expecting unaffected particles.

Native ECS helper `acceptSimGpuCueTick` is read-only and converts the existing `SimEvent::type` and fixed-point world positions into the generic `GpuCueEvent`. It does not retain ECS references or write VFX renderer handles into ECS. No FPS/RTS/other genre code is imported by the engine bridge.

### Example host scheduling

```cpp
#include <aiverselib/genre/SpectraFX.h>
aiverse::spectra::GpuPoolWorld pool;
pool.init(device);
pool.defineGraph("impact", impactGraph, aiverse::Json::object(), options);
aiverse::spectra::GpuCueBridge bridge(pool);
bridge.bind(gameImpactEvent, "impact");

// Once after a committed deterministic tick:
aiverse::spectra::acceptSimGpuCueTick(bridge, committedTick, simEvents);
// One presentation frame, device frame already open:
pool.beginPresentationFrame(cam, viewProjection, fbWidth, fbHeight);
bridge.flush();
pool.step(dt);
// Inside scene render pass:
pool.draw(fog);
// After scene render pass:
pool.endPresentationFrame();
// On rollback outside render/compute pass:
if(!bridge.resetForRollback()) { /* retry safely when device allows */ }
```

### Verification actually performed

**SOURCE INSPECTION, GITHUB CONNECTOR COMMITS ONLY. NO TESTS RUN.** The user explicitly directed finishing functionality first and testing later. No CMake, CTest, MSL compile/execution, editor UI interaction, GPU readback, native FPS/RTS integration, graphics profiling or actual render screenshots were run. No PASS result or release-ready claim is justified by the recorded code commits.

When testing is requested later, run the unified `./run-tests.sh` as specified in AGENTS.md; cover duplicate/out-of-order tick refusal, queue/tick/frame budgets, event rebinding while queued, named instance snapshots, unsupported CPU effect fallback, graphics pass ordering, failure telemetry, rollback reset retry, two unrelated genres using the same event adapter, and actual Metal visual parity.

### Known limitations / precise next functionality

1. **One-shot only.** The existing `ComputeParticles` and `GpuPoolWorld` APIs only spawn additive bursts and can clear all effects, but do not expose a per-source GPU emitter handle with selective kill/stop/retarget. The bridge therefore does NOT claim lifecycle management of persistent ECS-attached GPU effects. Use the already-existing CPU `CueScheduler` for continuous sources. Implement generation-safe GPU emitter ownership/selector buffers before making that claim.
2. Whole-pool rollback is intentionally destructive to all visual effects in this pool. Multiple independent rollback scopes need separate pools or a future per-source GPU kill mechanism.
3. No auto-migration of existing RTS/FPS native clients, no new gameplay ECS system, and no complete Unreal Niagara material/shader parity. Current Metal feature path remains experimental; GPU material support is already owned by a parallel agent and should not be duplicated.
4. **Next bounded Stage 30:** add CPU-owned, generation-safe GPU source IDs and GPU-buffer source tags plus per-source selective kill/retarget without resetting unrelated particles; integrate this with pooled continuous emitter scheduling and event-born particle provenance. Keep controls opt-in, continue user-deferred tests and commit a new handoff afterward.


## 2026-11-20 — Inspector graph-v2 integration with current main

Implementation `9db6d2014137e41cd67ff82dc187cf1136f73bb3`; branch `feature/aiv20261120_perf_inspector_integration`.
Editor build PASS; final SpectraFX 20/20 PASS including five hardware Metal tests;
actual PRISMA frame checks 5/5 PASS. Source-compatible view adapters and GPU
preview retirement corrections preserve newer main ownership. No experimental
defaults enabled. Full repo/Release/Vulkan/GLES/manual editor UI NOT RUN.
Exact commands, earlier failures, warnings, APIs and next steps:
`docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`. Separate documentation commit follows implementation.


## 2026-11-20 — Inspector revalidated with lighting main

Combined implementation `4acfb482742d2bb1d615c3eaae60308af09ffa1b` preserves incoming main `d84e958b1`.
Editor build PASS; SpectraFX 20/20 PASS (16.78s), five real Metal checks with
no skips; actual PRISMA frame review 5/5 PASS. Two unused GraphWindow helpers
removed; cleanup build has zero warnings. Full repository/platform/manual UI
coverage remains unrun. Exact commands and integration decisions: `docs/agent-handoffs/inspector-spectrafx-lighting-sync-2026-11-20.md`.


## 2026-11-21 — Review fixes (SpectraFX + Json depth limit)

Branch `feature/aiv20261121_fix_spectrafx` (base `973b32970`), commits
`8419965`..`2793498`. Fixed: graph-v2 value memoization + step budget,
Json::parse depth limit 256, distinct SpectraFxGraphWindow id
(`spectrafx_vfx_graph`; SpectraGraphWindow keeps `spectrafx_graph`),
orphaned GPU pool group retirement (retirement-token fenced), cached GPU
emitter library without per-Play IO, CPU handle pruning, HybridPlayback
retirement by `completedFrameSerial()`, cue inbox Stop/Kill reserve,
GpuCueBridge retry of rejected cues, editor preview restart retry, fx_graph_preview
coordinate validation, typed-field undo. Deferred: batched GPU slot uploads
(needs scatter compute + Metal validation), ParticleSystem undefine for
redefined CPU fallback effects. Standalone g++ builds of vfx_graph_compiler,
spectrafx_contract, spectrafx_retirement, vfx_gpu_backend,
vfx_shared_batch_world, vfx_graph_runtime, vfx_cue_scheduler: PASS. Full
CMake/CTest, Metal and editor UI NOT RUN. Details:
`docs/agent-handoffs/review-fixes-spectrafx-2026-11-21.md`.
