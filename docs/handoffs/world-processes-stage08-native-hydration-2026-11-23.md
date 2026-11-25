# AIVERSE — Stage 08: real native map → ECS hydration

Date: 2026-11-23
Branch: `feature/world-processes-experimental`
PR: https://github.com/chromealex/AIVERSE/pull/112 (**DRAFT**)
Main: not merged.
Existing WIP: ECHO, FRACTURE, AETHER; seven other proposed systems remain pure policy prototypes.

## Design, ownership and compatibility

The Studio Stage07 authoring values live in the normal map
`entities[*].components.{EchoZone,FractureMember,AetherGasCell}` fields.
Prior to this stage, they were persisted/Undoable documents but NOT
hydrated by the shipped game. This stage supplies a real native project
adapter without an `engine/ -> libraries/` dependency or parallel world
authority.

The real GameCore (a `final` class, **not subclassed**) already creates map
entities during `GameCore::setupMap`, publishing a one-to-one
`GameCore::mapPlaced()` vector and its post-generation
`GameCore::matchMap()`. `NativeModules::load` calls optional
`NativeModule::setup` after this map spawn and before the Simulation
persistence registry seals. The new native adapter uses exactly those
existing contracts.

- `libraries/include/aiverselib/world/WorldProcessesMapHydration.h`:
  `authoring::hydrateMap(Simulation&,const Json& map,const
  std::vector<Entity>& placed)` pre-validates all supported self-contained
  component docs, exact entity count/Entity generation and the existing
  runtime budget caps. It installs only ECHO, FRACTURE and/or AETHER
  persistent schemas and systems when their component is present, then
  writes the validated data to **actual** ECS component pools. Invalid
  map configurations fail before new ECS components are constructed.
  Maps without supported components add no new persistence schemas or
  systems; legacy save v9 remains unchanged.
- `libraries/include/aiverselib/world/WorldProcessesNativeModule.h`:
  `WorldProcessesNativeModule : game::NativeModule` and
  `worldProcessesNativeDefinition()`. The module simply calls
  `hydrateMap(sim, host.matchMap(), host.mapPlaced())` during its
  standard `setup` lifecycle. It stores no authoritative state
  outside World.
- `examples/native/world_processes/{CMakeLists.txt,WorldProcessesModule.cpp,README.md}`:
  a ready-to-copy native module with ID `world_processes`,
  factory `createWorldProcessesModule`, CMake linkage to
  `AIVERSE::libraries` and opt-in `rules.json`
  `nativeModules:[{"id":"world_processes"}]`.
  This is automatic hydration **when the project builds/enables this
  module**, not an automatic feature of every unmodified game binary.
- Other custom GameModule hosts can call `hydrateMap` directly, with
  their own exact map-index-to-ECS-Entity vector.

## Tests authored

- `tests/world_processes_map_hydration_test.cpp`: two unrelated generic
  GameModule template hosts, ECS source data, opt-in native v10 save,
  v9 unchanged without config, COW/rollback checksum parity, invalid map,
  placement count mismatch and dead Entity guard.
- `tests/world_processes_gamecore_native_test.cpp`: actual
  `game::GameCore` and its real `NativeModule` factory, using the
  existing template `mushrooms/maps/duel.json`. Confirms map positions
  map to real spawned ECS Entities, save v10, restart/reload,
  deterministic peers and COW, malformed config refusal, and no
  component pool/save format change without opt-in.
- `tests/world_processes_verify.sh` adds lightweight map hydration
  to the prior focused native GCC/Clang/ASan/UBSan tests.
- Root `CMakeLists.txt`: registers both
  `world_processes_map_hydration_test` (links engine+libraries) and
  `world_processes_gamecore_native_test` (links actual game+libraries).
  The latter is **not** in the standalone focused script (it requires
  the real game and template packages).
- `.github/workflows/world-processes.yml` watches the new test source files.

## SHA / changed files

- `5bc15bda1bdf58ff1c8c39daa5e8c61e62960a9e`:
  `WorldProcessesMapHydration.h`
- `03576cf769a6427468b31b4aaea8267334172353`:
  `WorldProcessesNativeModule.h`
- `05fb967ef932b705f9d299566379f8bb66ee0ff4`:
  generic map hydrate test
- `c59aaa134c0a7a16165077d571667a075aea6d38`:
  real GameCore native test
- `68e7555df45d104bf51b35eebe93be1746e0cc59`:
  focused verify runner
- `d7a9c98e305fa4e25b5daa0a2382f2cbee453b1c`:
  CMake registration
- `40922558ccd7242b5f6a9cffe663c2ff230724bb`:
  workflow watch paths
- `80950ad01097bc8fc1ea1023176f47d058a72ac0`,
  `c767e4e6188b17b1f6fe29d9c4d020828b9eb650`,
  `2b1f951b119ac19e11ab75d1a92b38a4f1bf2dc7`:
  native module example and README
- `a03e7876033ef7269c73bebfae4ac6f0e0219406`:
  agent skill updates.
- This document commit can be found with:
  `git log -1 -- docs/handoffs/world-processes-stage08-native-hydration-2026-11-23.md`.

## Verification status

The GitHub connector accepted the code, tests and CMake changes.
**Stage08's new tests have not yet been confirmed compiled or passed** on
a native Mac/Windows/Linux toolchain. The latest PR CI remains queued
at observation time. Do not label Stage08 green without checking
the actual workflow job logs.

Historical confirmed Stage05 focused suite:
https://github.com/chromealex/AIVERSE/actions/runs/45207631125
(4/4 GCC and Clang including Clang ASan+UBSan).
That evidence predates Stage06/07/08 and does **not** cover this code.

Previous global CI reported unrelated voxel layer violations; full
`./run-tests.sh --quick` on the merged Mac tree is still missing.
No real Studio/Metal screenshot evidence or final main integration.

## Remaining limitations / next exact steps

1. Inspect actual latest focused CI and real CMake compile output; fix
   the first error rather than guessing green. Run
   `bash tests/world_processes_verify.sh` and the two new CTest targets,
   including `aiverse_world_processes_gamecore_native_test`.
2. On the author's Mac, build with real project native sources and enable
   `nativeModules:[{"id":"world_processes"}]`; open map in Studio, add
   the three components, edit/save/reopen, start the game, and verify Live
   Inspector reads the actual ECS values. Capture genuine screenshots.
3. Add a deterministic, stable-id reference resolver and Studio object
   chooser for linked components: ECHO Portal/Emitter/Receiver,
   FRACTURE Bond, AETHER Duct/Flux. Reject unresolved/ambiguous ids;
   never use slot indices or approximate proximity as a substitute.
4. For PHYSIS interactions, use existing PHYSIS stress/fire owners and
   host-owned durable outboxes; do not add another physics or weather state.
5. Reconcile current main and concurrent `feature/studio-html5` edits,
   run full merged-tree Mac quick gate, and only then request merge review.
   PR stays draft.
