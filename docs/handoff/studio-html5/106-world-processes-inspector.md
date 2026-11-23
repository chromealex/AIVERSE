# Studio HTML5 — Stage 106: World Processes Live Inspector bridge (2026-11-23)

**Branch:** `feature/world-processes-experimental`, draft PR #112
**Scope:** read-only ECS inspection for ECHO, FRACTURE and AETHER. No new authoring
panels, no changes to Studio design system, no runtime mutation.
**Previous related Studio:** `docs/handoff/studio-html5/105-play-mode.md` and
`docs/handoff/studio-html5/83-live-runtime-inspection.md`.

## What changed

The HTML5 Studio already has the real-game route
`studio.gameInspect -> StudioRuntimeInspect -> editor::inspectEntity ->
StudioRuntimeEntity.components -> LiveInspector`.
No JS fake data or alternate editor representation is necessary.

- `libraries/include/aiverselib/world/WorldProcessesInspector.h`: a neutral,
  read-only ECS->JSON projection with twelve types and human-readable schema
  fields for EchoZone/Portal/Emitter/Receiver/Hearing,
  FractureMember/Bond/Collapse and AetherGasCell/Duct/Flux/FireCoupling.
  No component is shown unless it exists on the *current* ECS Entity.
- `apps/editor/core/WorldInspect.cpp`: after built-in and loaded native
  module readers, appends world-process components to the existing shared
  `components/componentSchemas` response. This is used by both the existing
  editor inspector and `studio.gameInspect` Live Inspector.
- `tests/world_processes_inspector_test.cpp`: verifies all twelve read-only
  schema cards and live component values, absence on empty/dead entities,
  generation-safe string ID resolution, idempotent projection and stale refs.
- `CMakeLists.txt`: `world_processes_inspector_test`; the focused
  `tests/world_processes_verify.sh` and GitHub
  `.github/workflows/world-processes.yml` include the new test.

## Tests and honesty

At the time this handoff is authored, **Stage106 newly added Inspector code
has no confirmed passing build or native/macOS UI screenshot**.
Previously verified *different* Stage05 C++ ECS tests: 4/4 GCC, Clang and
Clang ASan/UBSan in run
https://github.com/chromealex/AIVERSE/actions/runs/45207631125.
This is not evidence of the new Inspector integration.

Full status, code commit SHA and exact files are recorded in
`docs/handoffs/world-processes-stage06-studio-2026-11-23.md`.
New commits: `0051aca1719b8979cf612e6e0beadae196ce1ad7`,
`b6b36482341808bc03fb7b83ecee58586152c312`,
`522f80405fa04159490e6694e1b400c28892e339`,
`5c428aa280813c171b6f2585245497ee7ffc791e`,
`9337216cf9911100edf9a70a287af470e11142f5`,
`4ec2c0640240be97acec0fb660883b212db6d949`,
`5626f551b448b0c56430c67a40298c6d36cbaac2`.
This file commit: `git log -1 -- docs/handoff/studio-html5/106-world-processes-inspector.md`.

## Architecture and limitations

- All new values are read-only; none is editable through
  `studio.setProperty` or `studio.componentAdd`. Persistent Scene authoring
  requires explicit typed native operations plus safe map Undo.
- Only a *running game whose World actually contains these ECS pools* can
  show their cards. HTML5 Studio's browser-only mock keeps reflecting no real
  ECS gameplay. `studio.gameInspect` currently queries `GameCore` hosts;
  another native GameModule needs to participate in that bridge separately.
- No world-process dedicated panel, transform overlays, visual demo, or
  performance benchmark was added; screenshots are required once UI is
  actually running on a Mac.
- ECHO/FRACTURE/AETHER own only their ECS state and read existing PHYSIS
  environmental inputs; no genre-specific copies or engine->libraries edges.

## Exact next action

1. Inspect latest focused 5-test GitHub CI (GCC, Clang and ASan/UBSan);
   fix any regression on the committed source.
2. Run `npm run typecheck && npm run build` in `apps/studio-web`, and
   build the actual `aiverse_studio` native macOS host with correct KINGSTON
   build directory, then inspect real game entities through Live Inspector.
3. Verify component content matches World, missing pools aren't shown,
   read-only fields cannot mutate game state, undo is unchanged, and
   existing Inspector styling is preserved at 1440x900 and narrow widths.
   Record genuine screenshots and the exact successful test SHA.
4. Coordinate merge from `feature/studio-html5` without replacing the
   editor owner's concurrent work. Only after actual Mac merged-tree
   `./run-tests.sh --quick` passes should PR #112 be considered for main.
