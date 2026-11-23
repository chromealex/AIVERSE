# SpectraFX Stage 4 — Graph v2 typed Data Interfaces (2026-11-23)

## Provenance / status

- Repo: `chromealex/AIVERSE`.
- Branch: `feature/spectrafx-significance-data-20261123`; draft PR #113, **not merged to main**.
- Base: main `87ba5b1439267bea86c570489e8869b3f7641a0e`; main advanced in parallel to `aec89ef435aba457ebeec24ef196f62a2fbb611d`. Do not overwrite other agents' changes.
- Stage 4 commits, in order:
  - `29857ca1` Data Interface schema, typed validation, host callback registry and transactional snapshot
  - `4fcf12b7` integrate compile-time validation in graph v2 and accept DataInterface nodes
  - `40552309`, `0cc0157f` lower node to existing VM param slots and guard pure compile-time evaluation
  - `e5530015`, `ded19200` GraphRuntime per-burst sampling and lifetime
  - `46fcf5c2` GPU accepted-uniform mirror for retries
  - `f81f31e9` no-exception-host compatibility
  - `d14ab5f5`, `41f5fd3e` graph runtime contract tests
  - `cf0e5fc0`, `058b8cfd`, `9aca3439` shared authoring catalog, typed inspector property, transactional editing API
  - `e51874fd` data-driven demo asset
  - `9487bfcd` graph compiler/editor authoring test
  - `25857888`, `09fa575d` expand/fix Linux+macOS focused CI to 4 contracts
  - `b79a1c57` main system documentation
- Exact current branch HEAD may advance after documentation commits; always `git rev-parse HEAD` and quote the tested SHA before claiming PASS.

## Architecture and new APIs

1. **Single graph v2 runtime, no fork.** `engine/fx/SpectraDataInterfaces.h` provides `DataInterfaceBinding`, `DataInterfaceBindings`, `loadDataInterfaceBindings()`, `DataInterfaceRegistry` and `sampleDataInterfaces()` with finite ranges and bounded runtime cost. `dataInterfaces` is a map from existing `instanceParameters` names to `{provider,channel,scale,offset,fallback,required}`. Max 8 bindings, 16 provider callbacks; only safe names, scalar samples.
2. **Compiler.** `engine/fx/SpectraGraphV2.h` permits and validates the root `dataInterfaces`; `DataInterface` value nodes reference a declared binding/parameter, normalized to its slot. `engine/fx/SpectraRuntimeCodegen.h` lowers `DataInterface` into `param0..param7`; `SpectraValueGraph.h` declares the node runtime-only and rejects illegal constant-fold use. This preserves the existing `ParticleProgramModule` CPU/GPU path.
3. **Runtime.** `engine/fx/SpectraGraphRuntime.h/.cpp`: `GraphRuntime::setDataInterfaceRegistry(const DataInterfaceRegistry*)` after `initGraph`, `dataInterfaceBindings()`, `dataInterfaceStats()`. The instance samples its host snapshot at `burstAt(position)`, validates the complete batch and updates existing VM uniform slots before spawning. Required missing data rejects a burst; optional missing data uses authored fallback. Bindings/registry pointers are cleared at shutdown. New snapshots do not mutate earlier particles.
4. **Editor model.** `SpectraAuthoring.h` adds a DataInterface node to the shared node catalog and atomic `setDataInterfaceBinding` / `removeDataInterfaceBinding`; `SpectraModuleSchema.h` exposes a typed parameter name. Actual HTML5 Studio graphical editing and drag-drop are NOT implemented here. Existing editor/MCP asset JSON editing can validate/bake graph v2 as before.
5. **Example.** `examples/spectrafx/data_interface_weather_v2.vfx.json` demonstrates weather channel `vertical_acceleration` mapping into one named runtime parameter. `stableWeather` in docs is pseudocode for a game-specific immutable presentation snapshot, NOT an engine class.

## Tests and actual results

- Added to `tests/vfx_graph_runtime_test.cpp`: graph compilation/runtime VM lowering, required missing provider, optional fallback, transformed scalar values, atomic validation failure, per-burst dynamic sampling, shared CPU BatchWorld use, teardown, rejection of unsafe identifiers/out-of-range fallback/unknown instance param/unbound DataInterface node.
- Added to `tests/vfx_graph_compiler_test.cpp`: authoring node catalog and typed editor schema, valid/invalid atomic binding edits and removal.
- Focused workflow `.github/workflows/spectrafx-significance.yml` builds `aiverse_spectrafx_contract_tests`, `aiverse_spectrafx_header_smoke`, `aiverse_vfx_graph_tests` and `aiverse_vfx_graph_runtime_tests`; runs 4 CTests on Linux + macOS.
- Current Stage 4 **native compile/CTest/GPU hardware results are NOT YET AVAILABLE** as of writing. Previous pre-Stage-3 Ubuntu workflow reported 2/2 PASS, but those results do not apply to this SHA.
- Broad AIVERSE CI previously failed on unrelated `engine/voxel` layer edges (13), GameCore `override` warnings-as-errors, and Android plugin staging missing `modules`. Do not relabel these failures as new Data Interface regressions.

## Contracts / limitations

- Presentation-only, read-only provider callbacks, called on the rendering owner thread and **nonthrowing** (also for no-exceptions mobile/Web targets). Never capture or mutate a live ECS world. Host owns provider registry and must detach before destruction.
- Sampling frequency is **one sample per graph burst per bound parameter**, not per particle or every frame; existing instance parameter API still controls live-particle updates explicitly.
- CPU `ParticleSystem` and shared CPU `BatchWorld` preserve per-instance parameter semantics. GPU eligibility remains intentionally conservative and must be hardware tested; do not silently map unsupported graph ops into a partial GPU effect.
- A missing required provider fails new bursts before mutations. GPU uniform update failures are reported and an earlier binding restore is attempted; device loss still follows the GPU runtime's own recovery policy.
- Does not yet expose full vector/SDF/terrain/scene-depth data interfaces, indexed GPU storage buffers, PHYSIS/TerraWeave/NEREID concrete adapters, GPU shared arena field sampling, or HTML5 Studio inspector controls. This stage does not implement all proposed SpectraFX modernization features.
- The general `SpectraFxAccelerationFields` system from Stage 3 is separate from typed graph Data Interfaces: one operates on dedicated-emitter acceleration uniforms, the other on per-graph parameter slots. Future unified adapters must not double-apply forces.

## Next step — unambiguous

1. Inspect latest PR #113 head; run focused Linux/macOS workflow and fix **all** native compiler errors/tests against the exact SHA. Run CMake graph compiler/runtime tests and strict Metal acceptance.
2. Add real hardware parity test using a position-dependent `DataInterfaceRegistry`, forced CPU vs Metal VM; verify initial/remaining particle states and GPU lifetime/fence handling.
3. Add safe native immutable PHYSIS weather snapshot bridge through the registry (without introducing `engine/fx -> engine/physis` includes), and field channels for vector components via separate scalar parameter slots.
4. Synchronize with ongoing `feature/studio-html5` owner before integrating graph inspector/editor window; add typed data binding editor and UX tests there, not in a stale copy of another agent's branch.
5. Continue to Vulkan compute / collider Data Interfaces / GPU shared arena only in separate code+handoff stages. Keep PR draft until verified. Do not merge to main automatically.

## Mandatory handoff discipline

Every subsequent stage must write a **new** dated file in `docs/agent-handoffs/` with API, changed files, exact branch/SHA, **executed** tests + actual results, known limitations, and concrete next step. Update `docs/agents/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md` and `docs/spectrafx-modernization-roadmap-2026-11-23.md`.
