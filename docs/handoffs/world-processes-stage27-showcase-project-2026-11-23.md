# AIVERSE World Processes — Stage 27: generated real-project all-ten showcase

Date: 2026-11-23.
Branch: `feature/world-processes-experimental`, PR #112 DRAFT; `main` unchanged.
User request: continue feature implementation, **tests later**.

## Scope and sources

- `examples/world/world_processes_native/create_showcase.py`,
  commit `85407088b0ed38f493532b6c3777dbe0d67a9bb1`,
  preflight hardening `4530329a94710b4a5413d24346c4ea6b0bfb2e03`.
  This is an executable Python 3 standard-library *project copier and
  scene generator*, not a placeholder JSON demo and not a new game engine.
  It accepts a **real existing GameCore project directory** with
  `game.json`, `rules.json`, real authored map file, content assets and
  a caller-selected known rules-defined non-collider entity type.
  Copies the project to an explicit new destination (never overwrite),
  preserving its valid assets/rules and existing map content.
- Reads the existing shared source fragment
  `examples/native/world_processes/authoring-examples.json`.
  Rejects missing, duplicate or colliding stable map entity IDs.
  Places additional entities at spaced X/Z coordinates within the
  inherited map; adjusts `CrowdAgent` and `CrowdSlot` quantized
  centimetre coordinates to match those world locations. Adds
  dedicated editorial metadata for GRANULAR cell pitch and EVOLVE radius.
- Writes `maps/world_processes_showcase.json`, appends the map descriptor
  to the copied `game.json` manifest, and opts in the real
  `world_processes` module in the copied `rules.json` with the seven
  runtime categories enabled. Adds an explicit
  `WORLD_PROCESSES_SHOWCASE.md` with the remaining native compilation
  and game-owner integration instructions.
- The project **must still compile/link**
  `examples/native/world_processes/WorldProcessesModule.cpp` and use
  a real spawnable **non-collider** carrier type from its own rules.
  The generator cannot infer an arbitrary game's prefab rules or
  automatically create a real Box3D/Renderer3D actor and NPC callback.
  Source maps <20x20 or with insufficient space are refused instead of
  placing overlapping/demo entities outside the playing field.
- Documents in
  `examples/world/world_processes_native/README.md`,
  commit `ced0fd4bc7659e26daf85e6765449e9657380d42`,
  describe command invocation, actual project ownership, native
  module build and distinction between authoring map and gameplay
  physics/graphics integration.

## Relation to Stage26 Studio

Stage26 added real native Studio Scene OverlayShape gizmos for all seven
extended systems, while ECHO/FRACTURE/AETHER remain existing runtime/Inspector
owners. The demo map is a standard MapDoc project map; Studio can open it
through its usual project and map browser. The configured scene geometry
is **not** automatically inferred as Box3D contact shape/terrain chunk;
the optional native host adapters are invoked by the actual project
after the World ECS state is created.

Stage26 handoff:
`docs/handoffs/world-processes-stage26-studio-gizmos-2026-11-23.md`.
New header `apps/studio-native/StudioWorldProcessesGizmos.h` commit
`722e97f5e189c3f761a9eca1546100a357957ea3`;
host overlay integration `apps/studio-native/StudioHost.cpp` commit
`3406a2d5dba621b8176eba59ce1cbe1059eb7ea4`.
This stage's SHA can be found with
`git log -1 -- docs/handoffs/world-processes-stage27-showcase-project-2026-11-23.md`.

## Tests and limitations

**No tests ran**, per explicit request. In particular no execution of the
Python generator on a real AIVERSE template, no CMake build, no CTest,
no sanitizer, no HTML5 Studio screenshot, no Metal/Vulkan/GLES Scene
acceptance, no Box3D, and no current merged-tree Mac quick test.
Source committed to GitHub successfully but not yet build-proven.
Historic Stage05 focused pass predates these changes and is not evidence
for Stage26/27.

This is a **generator for a real game project copy**, not a guaranteed
playable/graphically complete world without selecting a real existing
carrier type, compiling the native module and connecting external
physics/AI/renderer owners. It does not automatically create assets
for all ten mechanisms.

## Specific next step

1. Finish advanced Scene gizmo interactions (select link endpoints,
   drag/edit in Scene with MapDoc Undo/Redo), coordinated with the
   concurrent Studio owner rather than duplicating HTML5 controls.
2. Create a proper content scene in a known project with actual model
   binding and a native WorldProcessGameplayHost callback that drives
   a CIRCUIT door and SOCIETY NPC action.
3. In the consolidated test phase after user's approval, execute the
   showcase generator on a real supported game/template, compile and
   run the resulting project on Mac, inspect real Scene output and
   save/rollback/replay, and preserve screenshots with provenance.
4. Reconcile current main/studio-html5 and full `./run-tests.sh --quick`.
   Only then merge PR #112.
