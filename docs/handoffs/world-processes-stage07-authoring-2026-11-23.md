# AIVERSE — Stage 07: guarded Studio map authoring for World Processes

Date: 2026-11-23
Branch: `feature/world-processes-experimental`, draft PR #112.
Merge status: **NOT MERGED**. Existing ECS WIP: ECHO, FRACTURE, AETHER. Seven other world-process suggestions remain initial pure policies.

## Scope

Adds safe **edit-mode map authoring** for three self-contained fields/groups of the existing ECS systems:
`EchoZone.enabled`, `FractureMember.anchored`, and `AetherGasCell.{smoke,toxin,oxygen,enabled}`.
This does NOT make arbitrary live ECS data editable, does NOT implement source/portal/bond
object picking, and does NOT automatically hydrate ECS components in every game module.

## Changed files and API

1. `libraries/include/aiverselib/world/WorldProcessesAuthoring.h`
   - pure, shared runtime-independent validation of small supported authoring schemas.
   - `supports(componentId)`, `defaults(componentId)`,
     `validate(componentId, Json, error)`, `edit(componentId,key,value,Json&,error)`.
   - exact signed integer parsing of JSON number literals with `std::from_chars`; rejects
     decimal, exponent, negatives, overflow, strings, absent/extra fields, unknown components
     and simulation-only fields like `broken/detached/sampledTick`.
   - `apply(World&, Entity, componentId, document, error)` explicitly creates
     the **actual** ECS component at native project setup when the host resolves
     its stable authored-document ID to a generation-safe runtime Entity.
     Rejects dead entities, missing/malformed values and duplicate ECS components.
     The editor does not silently install any runtime state.
2. `apps/studio-native/StudioSession.cpp`
   - `studio.componentCatalog` exposes three authorable components on map entities
     and correctly marks existing ones unavailable.
   - `studio.componentAdd` creates a real `extra.components[ComponentId]` record
     via `promotedAuthored`, using the same MapDoc undo snapshots as MeshRenderer;
     `studio.componentRemove` uses existing generic removal path.
   - `StudioSession::inspectorFor` renders supported valid field values using existing
     `genericField` + editable flags, integer min/max/step; invalid imported documents
     remain read-only rather than being silently repaired.
   - `studio.setProperty` routes known component IDs through exact validation and
     records a single `MapDoc::beginEdit()/touched()` if and only if the data changed.
     `studio.setProperties` already provides atomic multi-field and multi-entity
     undo/redo with `MapDoc::abandonSince` on failure.
   - Saving uses ordinary `MapDoc` project persistence. No new browser RPC, React
     palette or UI mutations, no editing of the live Simulation.
3. `tests/world_processes_authoring_test.cpp`: direct schema, failure safety,
   int parsing, authoritative `World::add`, generation/destroy guard and World snapshot.
4. `apps/studio-native/tests/StudioSessionTests.cpp`:
   validates add/duplicate rejection, edit, batch atomicity and failed rollback,
   protected runtime-only fields, Undo, Redo, correct Inspector editable metadata,
   `studio.save` and disk `MapDoc::load`, removal.
5. `tests/world_processes_verify.sh`, `CMakeLists.txt`,
   `.github/workflows/world-processes.yml`: adds authoring test to focused
   GCC/Clang/ASan + CTest and relevant workflow path triggers.
6. `skills/world/aiverse-world-processes/SKILL.md`: native authoring and runtime
   hydration contract.

## Code SHA ledger

- `74c3f74d9f64c031efe15d16227492d4fbc751ff`: authoring config and ECS apply API.
- `dad3867e6bd90c7ba804c6c4a6b574e53d5da424`: StudioSession editor authoring/undo code.
- `6daf3afdb51a0195656bf024464e08339c69d12b`: include explicit numeric limits.
- `783f169ec5d24fc63dcc66bda0401d5a58325a76`: library authoring test.
- `b559327ed01effb5a3fc5d77b806ec7dd2915d12`: native StudioSession test.
- `6633ec9ac649e7fe7834fdd97a862932b3daf0f5`: corrected test JSON escaping.
- `c37b965352ec40789f38781006b2abf9df1b35e1`: focused runner.
- `6962b6815e4de33fc51ed1369f713bff9ccf487e`: CTest.
- `ae3aa6520a6ed31c1fa4a8ce3c4854670f7ea69f`: workflow triggers.
- `ef690d7219a70d850c662bca120cda052d21bff3`: agent skill.
- Handoff SHA: `git log -1 -- docs/handoffs/world-processes-stage07-authoring-2026-11-23.md`.

## Verified vs NOT verified

Previously **verified** Stage05 focused 4/4 GCC/Clang/Clang ASan+UBSan:
https://github.com/chromealex/AIVERSE/actions/runs/45207631125

The new Stage06/07 Inspector and MapDoc authoring code was submitted to
GitHub but **no green compilation / CTest / native macOS Studio runtime evidence**
has been observed at the point of this handoff. New tests are authored, NOT
confirmed passing. Earlier overall PR CI failed for unrelated
`engine/voxel -> libraries` edges; do not mark entire repo green.
Do not merge PR #112 or claim complete authoring yet.

## Next specific steps

1. Inspect the latest focused workflow and Studio validation after this handoff;
   use job compiler logs to fix the *first actual* errors without loosening tests.
2. On the author's Mac with separate build directory, run
   `bash tests/world_processes_verify.sh` and
   `cmake --build <build> --target aiverse_world_processes_authoring_test aiverse_world_processes_inspector_test aiverse_ecs_echo_acoustics_test aiverse_ecs_fracture_structure_test aiverse_ecs_aether_gas_test`.
   Run CTest with regex `^(world_processes_(authoring|inspector)_test|ecs_(echo_acoustics|fracture_structure|aether_gas)_test)$`.
3. Run the native StudioSession test target (check CMake's actual target name)
   and manually open an authored map, edit gas/anchor/zone parameters,
   save, Undo/Redo, restart Studio, verify disk persistence and no read-only
   runtime mutation. Capture genuine Studio screenshots.
4. Implement a **real native project consumer** that resolves map authored IDs,
   calls `authoring::apply` once at spawn, and configures ECHO/FRACTURE/AETHER
   full graph references through stable IDs. Do not write a second ECS authority.
   Confirm values have gameplay effects after map reload before calling this complete.
5. Add validated object references for EchoPortal/from/to, FractureBond members,
   AetherDuct/from/to via the existing asset/entity picker design and native
   reference resolution (generation-safe, deterministic). This is NOT done.
6. Pull/reconcile latest `main` and `feature/studio-html5` without overwriting
   concurrent edits; run `./run-tests.sh --quick` on merged Mac tree.
   Only then consider main merge.
