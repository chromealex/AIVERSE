# Studio HTML5 — Stage 107: world-process map authoring, validated Undo/Redo

Date: 2026-11-23
Branch: `feature/world-processes-experimental`, draft PR #112.
This is a staged extension of Studio Stage106 Live Inspector; it does not
replace or redesign `feature/studio-html5` or its UI components.

## Scope
Scene Inspector can add, remove and edit real serialized
`DocEntity.extra.components` authoring data for three independently useful
world-process component types:
- `EchoZone.enabled`
- `FractureMember.anchored`
- `AetherGasCell.smoke/toxin/oxygen/enabled`

The existing inspector field UI is reused (no one-off palette or dialogs),
with exact integer validation, field bounds, one-Undo-step batch mutation
and disk persistence. Runtime-only `broken`, `detached` and
`sampledTick` values remain read-only and cannot be forged in authored maps.

## Architecture and changes
- `apps/studio-native/StudioSession.cpp` routes these three component types
  through `studio.componentCatalog/componentAdd/componentRemove/setProperty`
  and normal `MapDoc` snapshot/Undo/Redo/save. `studio.setProperties` uses
  its existing batch rollback on failure. UI only emits the already-whitelisted
  bridge methods. All writes reference a validated real map entity ID.
- `libraries/include/aiverselib/world/WorldProcessesAuthoring.h` is
  genre-independent typed validation and optional native ECS spawn API:
  `authoring::apply(World&,Entity,id,Json)`. The host must call it at setup
  with a verified map ID->ECS Entity mapping. The browser does not write ECS.
- `apps/studio-native/tests/StudioSessionTests.cpp` has add/edit/undo/redo,
  invalid parameter rollback, serialized save/reload and component removal.
- `tests/world_processes_authoring_test.cpp` tests the pure runtime consumer
  and strict malformed inputs. CI runner, CMake and agent skill updated.

Exact changed files, code SHAs and required acceptance steps:
`docs/handoffs/world-processes-stage07-authoring-2026-11-23.md`.

## What is not finished
No automatic integration of authored map components into existing game
module creation is claimed. A project native module must explicitly apply
validated data when the entity spawns. Relationship references like
ECHO portal endpoints, FRACTURE bond links and AETHER ventilation links are
not yet authorable: they require the shared locked object selector + stable
runtime Entity mapping and save/replay tests. The Live Inspector remains
read-only, preserving one authoritative ECS simulation state.

The Stage07 code was pushed, but **new tests and macOS GUI validation
were not yet verified** at documentation time. Previously verified Stage05
four-test GCC/Clang/ASan workflow:
https://github.com/chromealex/AIVERSE/actions/runs/45207631125.
Full Studio and merged Mac quick gates are pending.

## Exact next stage
1. Inspect focused CI, Studio validation and CMake/native acceptance results;
   fix compile/link failures without broad changes.
2. Run macOS Studio, open a fixture project, add three new components, edit
   valid/invalid values, Undo/Redo, Save, close/reopen and verify correct
   Scene Inspector authoring against real document data.
3. Wire a native project spawn/hydration hook and test values affecting
   gameplay under deterministic replay. Only after that claim game-authoring
   parity. Then add object-reference picker and generic authoring metadata.
4. Add genuine screenshots for this user-visible Stage107 after native
   viewport/Inspector QA; do not synthesize captures.
5. Preserve concurrent `feature/studio-html5` work, update the handoff after
   every code stage, and do not merge without `./run-tests.sh --quick` green
   on a reconciled Mac tree.
