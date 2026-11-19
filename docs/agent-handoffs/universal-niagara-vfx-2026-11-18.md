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
