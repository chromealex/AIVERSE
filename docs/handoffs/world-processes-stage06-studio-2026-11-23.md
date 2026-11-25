# AIVERSE world processes — Stage 06: HTML5 Studio live ECS Inspector (2026-11-23)

Repo: `chromealex/AIVERSE`
Branch: `feature/world-processes-experimental`
Draft PR: https://github.com/chromealex/AIVERSE/pull/112
Main: unchanged (do not merge until full Mac quick gate).

## Scope and ownership

Use the existing editor/native Studio `WorldInspect::inspectEntity` pipeline:
`apps/studio-native/StudioRuntimeInspect.h` -> `editor::inspectEntity` ->
JSON `components/componentSchemas` -> `studio.gameInspect` ->
`apps/studio-web/src/panels/LivePanels.tsx` (live read-only list).
No additional frontend panel, fake fixtures or new IPC operation.

- `libraries/include/aiverselib/world/WorldProcessesInspector.h`: new
  `aiverse::lib::worldprocess::inspector::appendWorldProcessComponents(
  const World&,Entity,Json& components,Json& schemas)`.
  It reads **actual** ECS components of the selected live Entity using
  `tryGetConst` and appends 12 read-only component cards:
  `EchoZone`, `EchoPortal`, `EchoEmitter`, `EchoReceiver`,
  `EchoHearing`, `FractureMember`, `FractureBond`,
  `FractureCollapse`, `AetherGasCell`, `AetherDuct`,
  `AetherFlux`, `AetherFireCoupling`.
  The helper neither creates a World pool nor writes to simulation.
  Missing components are never fabricated and existing component keys
  take precedence over the helper.
  All 12 schema cards mark every field `editable:false`.
  Entity refs store exact generation-safe `packed` as a decimal STRING,
  plus an `alive` flag. 64-bit event keys/ticks that might exceed JS
  precision are also strings. No in-game visual or editor authoring
  capability is implied.
- `apps/editor/core/WorldInspect.cpp`: invokes the helper after loaded
  native module introspection and before publishing `components` and
  `componentSchemas`. Existing editor and HTML5 Studio live runtime
  introspection share this implementation. This uses normal app->library
  dependency direction; no engine->libraries edge.
- `tests/world_processes_inspector_test.cpp`: tests all 12 component cards,
  schema read-only, exact packed refs, stale generation refs, ignored
  repeated calls, no synthetic values, and absence on dead/empty entities.
- `tests/world_processes_verify.sh`: adds the inspector regression to the
  previous four focused policy/ECS tests.
- `CMakeLists.txt`: adds `aiverse_world_processes_inspector_test` with
  CTest name `world_processes_inspector_test`.
- `.github/workflows/world-processes.yml`: watches relevant editor and
  inspector tests. Focused matrix uses GCC/Clang, with Clang ASan+UBSan.

## Commits

- `0051aca1719b8979cf612e6e0beadae196ce1ad7`: world process
  inspector JSON schema projection.
- `b6b36482341808bc03fb7b83ecee58586152c312`: library-local includes
  for IDE/editor integration.
- `522f80405fa04159490e6694e1b400c28892e339`: inspector test.
- `5c428aa280813c171b6f2585245497ee7ffc791e`: integrate into
  `WorldInspect::inspectEntity` (Studio/Editor common path).
- `9337216cf9911100edf9a70a287af470e11142f5`: focused script.
- `4ec2c0640240be97acec0fb660883b212db6d949`: workflow path.
- `5626f551b448b0c56430c67a40298c6d36cbaac2`: CTest.
- This handoff commit: `git log -1 -- docs/handoffs/world-processes-stage06-studio-2026-11-23.md`.

## Verification — actual vs unverified

Previously **verified** on commit `b26b91ad9db3e3d456e7ee3dc7b985815dd062b5`:
[World processes focused ECS acceptance](https://github.com/chromealex/AIVERSE/actions/runs/45207631125)
GCC success; Clang normal success; Clang ASan/UBSan success; four
pre-existing focused tests passed.

**New Stage06 Inspector code/test is not yet confirmed compiled or passed**
at the moment of this handoff creation. Do not claim Studio native UI was
visually inspected, or that `aiverse_editor` / `aiverse_studio` builds pass.
Full CI, Mac `./run-tests.sh --quick`, main reconciliation and engine
integration are still pending. Earlier layer CI detected 13 unrelated
voxel->library violations; do not hide or reclassify them.

## Exact next action

1. Inspect the latest focused GCC/Clang/ASan+UBSan CI after Stage06 commit;
   fix any compiler errors in the Inspector helper without disabling warnings.
2. Compile full `aiverse_editor` and `aiverse_studio` on Mac and run
   `ctest --test-dir <build> -R '^(world_processes_inspector_test|ecs_echo_acoustics_test|ecs_fracture_structure_test|ecs_aether_gas_test)$' --output-on-failure`.
3. Create real authored test content (ECHO zones/portals, FRACTURE joints,
   AETHER ducts) via existing project native setup; run the game and select
   those entities in **Live** Inspector. Verify only actual components appear,
   names/values are accurate, no field is editable, and FPS/RTS/rules
   inspection is unchanged.
4. Do not claim authorable Scene Inspector: this stage only exposes LIVE
   read-only ECS values. Full Studio authoring needs validated native
   component add/patch/remove APIs, `MapDoc` persistence/undo, and
   schema-driven asset picker; coordinate with `feature/studio-html5`.
5. Run `./run-tests.sh --quick` on the merged Mac tree before main, then
   update the handoff with exact SHA, pass counts and remaining blockers.

Seven other proposed mechanics remain Stage01 proof policies; WIP stays
ECHO/FRACTURE/AETHER only.
