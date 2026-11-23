# Functional building recipes — setting-independent properties

## Model

Visual appearance and functional capability are separate dimensions. Setting/style
selects materials and colours. A functional recipe supplies actual wall openings,
roof apertures and movable panels. It does NOT select gameplay role factory,
barracks, depot or command. Existing legacy preset calls remain compatible.

Current kinds:
- large-gate: 1..8 vehicle openings, metres-based clear width/height, wall side, 0..1 open state
- small-gate: 1..8 small-gate openings, same parameter types
- personnel-door: 1..8 personnel entrances
- sliding-roof: one real roof aperture with two sliding panels, width/depth in metres, 0..1 open state

Four sides: front, back, left, right. Geometry validates non-overlapping space
per wall, building bounds and panel sizes before generating a mesh. Walls are
constructed as solid segments around openings and share collision shapes; the
roof ring excludes its true aperture, and retracted roof panels remain visible
on the solid side deck. Style-derived material slots affect appearance only.

## Example

File: engine/assets/building-recipes/multiuse-open.json

    {
      "width":24,"height":12,"depth":18,"seed":7,
      "features":[
        {"kind":"large-gate","side":"front","count":1,"width":5,"height":4.5,"open":1},
        {"kind":"small-gate","side":"back","count":2,"width":2.7,"height":2.9,"open":1},
        {"kind":"personnel-door","side":"front","count":1,"width":1.2,"height":2.35,"open":1},
        {"kind":"sliding-roof","count":1,"width":5,"depth":5,"open":1}
      ]
    }

Change only the --style input (scifi vs modern): the structure functionality
and actual aperture/collision geometry remain identical. The appearance changes.

    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/scifi.json \
      --recipe engine/assets/building-recipes/multiuse-open.json \
      --out build/buildings-qa/scifi-multiuse.glb

    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/modern.json \
      --recipe engine/assets/building-recipes/multiuse-open.json \
      --out build/buildings-qa/modern-multiuse.glb

## Native game API

CPU implementation: libraries/include/aiverselib/systems/BuildingFeatureGenerator.h.
Runtime API: libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h.
Use generateFunctionalBuilding(style,recipe,origin,yaw) directly, or register
a style with RuntimeBuildingGenerator and call generateRecipe(styleId,recipe).
The latter returns geometry, conservative collision solids, gate anchors and
featureAnchors for doors and the sliding roof. Convert geometry using
engine/buildings/RuntimeBuildingModel.h to Renderer3D Model; GPU upload and
retirement belong to the game render owner.

An independent game build needs neither Studio nor the editor, and styles
can be loaded from packaged asset bytes. The recipe is independent of genres.

## Studio

Building Generator now defaults to Functional properties, with add/remove
controls for kind, wall side, count, aperture width/height/depth and open state.
Legacy silhouette presets remain selectable in the alternative mode. Native
Studio creates the generated .glb and a canonical .glb.recipe.json next to it
under project/content/GeneratedBuildings, then updates the existing Asset
Catalog/.meta/GUID. Browser JS never writes project files directly.

## Testing on macOS

    clang++ -std=c++20 -O1 -Wno-misleading-indentation -I. -Ilibraries/include \
      tests/libraries/building_feature_tests.cpp engine/core/Fixed.cpp \
      engine/data/Json.cpp -o build/buildings-qa/building_feature_tests
    ./build/buildings-qa/building_feature_tests .

The test covers wall collision holes, shutter open/closed, roof open/closed,
cross-style identical geometry, yaw, error validation and runtime recipe API.
Native Studio acceptance also checks GLB export and the saved JSON recipe.
Actual latest C++/TypeScript/Metal verification has NOT yet completed.

## Boundaries / follow-up

This new functional shell is a simple modular building, not yet a full
fusion with the earlier sci-fi armour/curved massing grammar. The old
armoured building presets still have visual doors on closed hulls; do not
claim those are enterable. Open fractions currently generate a new CPU
mesh/collision state; smooth real-time animation needs moving GPU-owned
subnodes and safe physics/navmesh publication rather than regeneration
every frame. Next add windows, loading ramps, hinged/roller shutters,
roof landing pads, service hatches and positional placement per face.


## Exact location and StructureGraph

See docs/building-structure-graph.md. Wall property `position` (metres along wall; -1 auto),
`spacing` (clear gap for repeated opening count) and roof `position` (X) /
`positionZ` (Z) are authoritative. Graph computes structural solids,
collision and stable named sockets from the same layout.


## Stage 12: Windows, room grid and multiple floors

The core FunctionalBuildingRecipe now supports floors (1..8), roomsX and roomsZ
(1..6 per axis, with at most 128 total cells). A floor requires at least 2.8m
nominal vertical clearance and each room cell at least 2.5m width/depth.

New feature kind window is a real exterior wall aperture with a separate 5cm
glazing solid, not a texture on a full wall. Specify feature.floor as the
zero-indexed storey and feature.sill in metres relative to that storey.
Windows have a clear width and height, count, side and exact position.
Glazing gets its own physical collider; windows are not walkable entrances.

BuildingStructureGraph includes room rectangles and horizontal connections.
Room IDs follow room/<floor>/<column>/<row>. Horizontal internal passage IDs
follow passage/x/... and passage/z/...; partition geometry and collision
contain true 1.2m wide door cutouts. The graph is plain C++20 with no
material/renderer/game/editor dependency. RuntimeBuildingResult.structureGraph
exposes this topology from a packaged game build.

Example: engine/assets/building-recipes/two-floor-workshop.json:
24x14x18m, 2 floors, 2x2 rooms per floor, upper- and lower-floor windows,
a vehicle gate, personnel door and sliding roof. The same recipe can be
exported with scifi or modern material style.

Run in the isolated Mac worktree after fetch/switch:

    mkdir -p build/buildings-qa
    clang++ -std=c++20 -Wall -Wextra -Werror -Ilibraries/include \
      tests/libraries/building_structure_graph_tests.cpp \
      -o build/buildings-qa/building_structure_graph_tests
    ./build/buildings-qa/building_structure_graph_tests
    clang++ -std=c++20 -O1 -Wno-misleading-indentation -I. -Ilibraries/include \
      tests/libraries/building_feature_tests.cpp \
      engine/core/Fixed.cpp engine/data/Json.cpp \
      -o build/buildings-qa/building_feature_tests
    ./build/buildings-qa/building_feature_tests .
    python3 tools/buildings/qa_batch_20.py --compile --allow-failures
    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/scifi.json \
      --recipe engine/assets/building-recipes/two-floor-workshop.json \
      --out build/buildings-qa/two-floor-workshop.glb

LIMITS: Floors remain DISCONNECTED vertically. No stairs, elevators, floor-slab
openings, vertically connected navmesh or live physics updates exist yet.
Horizontal window wall-span reservation is shared across floor elevations, so
windows cannot stack at the same wall coordinate on different floors yet.
The room layout is a rectangular grid, not arbitrary floor-plan geometry.
Window glass is static. No engine GPU render screenshots have been verified
for this stage. GitHub focused CI was queued at implementation time.


## Stage 13: Real floor shafts and vertical circulation

The functional recipe now supports two additional style-neutral property kinds:
**stairs** and **elevator**. These are independent of a building's gameplay
role or style. A sample is located at
engine/assets/building-recipes/vertical-circulation.json.

Stairs and lifts have width and depth (metres), optionally X/Z centre
(position and positionZ; -1 means automatic), count exactly 1 and floor 0.
They require at least two storeys. The generated shaft stays inside a single
room cell, with 0.35m structural margin, avoiding exterior/perimeter walls and
internal partitions. At most two vertical cores may be authored per recipe;
intersecting cores or cores that would cross partition walls are rejected.

The StructureGraph produces **real empty holes** in all upper-storey floor
slabs, not merely visible hatches. The slab mesh and conservative physics
colliders both come from the same rectangular partition of the surface.
For stairs, two opposed flights of box treads and a turning landing are
generated per storey; rise is capped at 0.18m and tread depth is at least
0.24m. The stairwell must be large enough for these constraints. For lifts,
four thin fixed guide rails and a **single starting platform** are generated.
The lift platform is NOT animated automatically.

Graph connections have BuildingConnectionKind:
Passage (same-storey), Stair (between adjacent storeys), Elevator (between
adjacent storeys). Vertical connection IDs are vertical/<feature>/<floor>,
with source/target room IDs. Stable semantic sockets include
stairs/<feature>/<floor> and lift/<feature>/<floor>. These are data and
geometry for a future gameplay/navmesh owner, not a claim that an existing
AI agent already walks the stairs or calls an elevator.

Studio Building Generator exposes Stairs and Lift properties, including
shaft W/D and X/Z position, and persists them through the existing native
GLB + canonical recipe JSON asset path. RuntimeBuildingGenerator returns
all graph connections/sockets without depending on Studio/renderer/genre.

### On the Mac

    cd ~/Projects/AIVERSE-buildings-qa
    git fetch origin feature/aiv20261123_building-grammar
    git status --short
    git switch --detach origin/feature/aiv20261123_building-grammar
    mkdir -p build/buildings-qa
    clang++ -std=c++20 -Wall -Wextra -Werror -Ilibraries/include \
      tests/libraries/building_structure_graph_tests.cpp \
      -o build/buildings-qa/building_structure_graph_tests
    ./build/buildings-qa/building_structure_graph_tests
    python3 tools/buildings/qa_batch_20.py --compile --allow-failures
    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/scifi.json \
      --recipe engine/assets/building-recipes/vertical-circulation.json \
      --out build/buildings-qa/vertical-circulation.glb

The focused workflow also exports the same recipe under modern materials
and runs native/cross-style tests. These changes have been committed but
Stage 13 C++/TS/native/GPU test results have NOT been verified at the time
of documentation. Do not merge or claim playable vertical navmesh until
the existing PHYSIS/Navmesh/gameplay owners are integrated and visual QA passes.


Stage 14: runtime navigation/lift control and dynamic collision:
see docs/building-runtime-circulation.md. Static building geometry excludes
the moveable cabin in game runtime; Studio previews include it.



## Stages 14–15 — native navigation, lift ownership and AI route execution

The functional building graph provides horizontal Passage edges and vertical
Stair/Elevator edges, but the existing engine/nav/NavMesh.h does not expose
off-mesh links. Do not patch synthetic portals into the mesh. The adapter
engine/buildings/BuildingNavMeshBridge.h resolves real endpoints on native
nav polygons, finds same-floor native corridor paths, and returns explicit
vertical actions. Nav links are revisioned and invalid when navmesh becomes
dirty. Games must refresh native nav::Mesh from authoritative collision before
routing again.

The portable libraries/include/aiverselib/systems/BuildingCirculationRuntime.h
contains deterministic lift state (integer mm/ms), pickup/boarding/destination/
disembark actions, passenger capacity, replicated revisioned snapshots,
and integer platform movements. It is NOT a separate physics or ECS engine.
A single lift platform is authored as a dedicated dynamic Model rather than
included in the game's immutable static building geometry. Studio's GLB still
includes it to make a complete model for offline viewing.

libraries/include/aiverselib/systems/BuildingRouteExecutor.h is a portable
per-agent action state machine with steps Walk, TraverseStairs, CallLift,
WaitLift, BoardLift, SelectDestination, RidingLift, DisembarkLift. The owning
agent/Motor system must explicitly acknowledge reaching a real room, boarding
volume and destination room; a connection in the graph is NOT permission to
teleport or move through a collider.

engine/buildings/BuildingCirculationHost.h owns lift controllers and tracks
actor IDs already owned by ECS. It returns rider movement deltas alongside
platform movements. engine/buildings/BuildingCirculationScene.h publishes
dynamic elevator AABBs through existing world::SpatialWorld and marks native
navmesh tiles dirty. The returned publishedPlatforms/publishedRiders arrays
contain ONLY successfully applied platform collider updates; attempted
tick.platformMovements are diagnostic and MUST NOT be applied directly to
actors or rendered platforms. Collider publication failure faults the
scene and requires owner reconciliation; it cannot be hidden as success.

### Gameplay integration sketch (native client/server)

    #include "engine/buildings/BuildingRuntime.h"
    // 1. generated = runtimeGenerator.generateRecipe(...).
    // 2. Spawn static model/colliders via existing owning ECS/renderer.
    // 3. scene.attach(generated, spatialWorld, &navMesh, gameColliderIds,
    //                 origin, yaw, &error).
    // 4. Refresh native nav mesh from collision-backed Source.
    // 5. scene.rebuildNavigation(navMesh).
    // 6. For each agent, host.createRouteExecutor(executor, from, to).
    // 7. Each game tick:
    //      command = host.nextAgentCommand(executor);
    //      owner drives agent movement/boarding acknowledgements;
    //      host.pumpAgent(executor);
    //      result = scene.update(spatialWorld, &navMesh, deltaMs);
    //      owner publishes result.publishedPlatforms + publishedRiders.
    // 8. Rebuild native nav Source after dirty colliders and re-resolve links.

No stage-14 or stage-15 code has been compiled or run in this session:
user explicitly deferred tests. No claim of working ECS actor movement,
multiplayer, lift doors or completed GPU scene may be made from source alone.
Next: explicit ECS actor components, render/physics owner wiring, safe boarding
volume checks, door interlocks, rider replication, end-to-end Mac/Metal
demonstration and compiler/CI validation.


## Stage 17: authoritative ECS session entrypoint

Native client/server game code may use
engine/buildings/BuildingLiftGameSession.h (also included by
engine/buildings/BuildingRuntime.h). This is a template for an EXISTING
Body component with a writable Vec3 pos; it does not create a second ECS
or network stack. The game passes its World, SpatialWorld, nav::Mesh,
unique platform/door collider IDs, scene ID, and an actor authorization
callback from its own ownership/session system.

Sequence:

1. generateRecipe; create static CPU mesh with elevator platform excluded.
2. BuildingLiftGameSession<Body>.attach with current project scene identity,
   game-owned collider IDs, world transform and authorization callback.
3. attachDoors with game-owned leaf collider IDs, then refresh the native
   nav::Mesh using its existing collision-backed nav::Source.
4. board (requires genuine open landing door and feet in cabin volume),
   call/requestDestination/exit through game-authorized ECS entities only.
5. advance(ecs,spatial,nav,deltaMs,serverTick) on the authoritative world
   update phase. Only if tick.complete is true may the existing secure
   transport publish tick.frame. On error the scene is faulted.
6. After update, the game's renderer owner applies published platform
   instance transforms; game physics/actor owner has already moved ECS
   passengers via the existing EcsBridge, after collider publication.
7. Scene unload uses detachIfVacant; releaseAtLanding handles rider cleanup
   only if the lift is open at a genuine landing. Do not eject moving actors.

This is an owner API: it has not been wired into an actual running
game mode, and C++/native/Metal tests are explicitly deferred.


## Stage 18: physical ECS actor entry/exit and network publication

BuildingCirculationEcsScene<Body> binds a generated building to the existing
ECS World, SpatialWorld and native nav::Mesh. It provides a live doorway
obstruction probe using its existing Body component pool. BuildingLiftPassenger
is a small marker on the actor's existing ECS Entity, not a second actor store.
Boarding requires the physical cabin volume; disembark requires actor feet
at the correct OUTSIDE landing with open doors. Actor carry happens only after
the owning physics world successfully commits the lift platform AABB.
If publishing any actor fails, the scene faults and invalidates navigation.

After a successful owner-phase update, captureServerSnapshot(serverTick)
may encode the already existing bounded versioned lift/rider frame for the
game's authenticated server-to-client transport. No network socket or extra
replication protocol was introduced. An alternative BuildingLiftGameSession
owner requires an explicitly supplied authoritative obstruction callback in
attachDoors; sensorless attachment fails immediately. Do not instantiate
both scene owners for the same building. More details and limitations:
docs/agent-handoffs/building-ecs-physical-doors-2026-11-23.md.

User-requested tests remain deferred: no recent compilation or Metal visual
verification has demonstrated this ECS integration in a running game.
