# Runtime procedural buildings in packaged AIVERSE games

The CPU generator is not an editor feature: it belongs to the portable C++20 library
AIVERSE::lib_building_style. It supports generation after release on Mac,
Windows, Linux, mobile and (subject to compilation verification) Web/WASM.

## Game C++ API

~~~cpp
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include "engine/buildings/RuntimeBuildingModel.h"

using namespace aiverse::lib::systems;
RuntimeBuildingGenerator buildings;
std::string error;

// Callback is the game's own pack/asset reader, not Studio's filesystem.
bool ok = buildings.registerStyleAsset("building-styles/scifi.json",
    [&](const std::string& id, std::string& text) {
        return gameAssets.readText(id, text); // provide your game reader
    }, &error);

RuntimeBuildingRequest request;
request.style="scifi";
request.function="factory";
request.seed=482733;
request.size={24,12,18};  // W/H/D metres; all zero keeps style-defined dimensions
auto output = buildings.generate(request);
if (!output) {
    // output.error + output.message: no invented fallback model
} else {
    auto boxes=output.building.boxes; // pass to the physics/ECS owner
    auto gates=output.building.gates; // static entrance anchors
    auto cpuModel=aiverse::building::runtimeBuildingModel(output.building);
    // Hand cpuModel to the existing renderer owner. Only on its GPU thread:
    // render::uploadModel(device, *cpuModel)
    // Publish/retire via existing GPU lifetime mechanism.
}
~~~

No dependencies on Studio, editor-only build settings, GLB exporter or
external shell commands. Model conversion also runs on CPU. Mesh upload,
presentation publication and retirement MUST remain with the existing renderer
device-owner thread, never inside an authoritative ECS simulation tick.

The returned StyledStructure owns its materials, vertex/index buffers and
coarse collision AABBs. Do not assume bitwise identical floating-point meshes
across CPU architectures; replicate authoritative gameplay collision where
needed. Avoid mutating the style registry while worker threads generate.

## CMake and limitations

~~~cmake
target_link_libraries(my_game PRIVATE AIVERSE::lib_building_style)
# Renderer3D upload/conversion also needs your existing renderer runtime link.
~~~

Runtime API: libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h
Renderer CPU adapter: engine/buildings/RuntimeBuildingModel.h
Tests: tests/libraries/building_runtime_tests.cpp (CTest library_building_runtime),
tests/render/runtime_building_model_compile.cpp (compile-only).

Style registration is limited to 1 MiB and geometry to 150k triangles/400k
vertices. The exact W/H/D constructive solver can still fail for feasible sizes;
the API returns an explicit FitFailed instead of deforming/faking a model.
Generation on a worker, caching, ECS placement and GPU resource retirement
are integration responsibilities of the game. Native GPU and platform
release builds have not been validated at this commit.


Stage 12: RuntimeBuildingResult.structureGraph contains rooms and horizontal
connections. Multi-floor properties: floors, roomsX, roomsZ and window
features with floor/sill. Example: engine/assets/building-recipes/two-floor-workshop.json.
No vertical floor connections yet.


Stage 13: stairs/lift recipe support. See
engine/assets/building-recipes/vertical-circulation.json.
RuntimeBuildingResult.structureGraph.connections now includes
BuildingConnectionKind::Stair/Elevator across adjacent floors, and solids
have physically absent upper slab sections. A gameplay owner must publish
navmesh links and implement actual elevator platform motion separately.


## Circulation runtime, Stage 14

Native game client may include engine/buildings/BuildingRuntime.h.
After generateRecipe(), call BuildingCirculationHost::initialize().
For each lift, create runtimeLiftPlatformModel() ONCE and publish its
unique dynamic collider through BuildingLiftColliderOwner. Build/refresh
nav::Mesh, then host.rebuildNavigation() and host.route().
Lift sequence: callLift(pickup), boardLift, requestLiftDestination,
advance(deltaMs), publish platform movement in SpatialWorld owner phase,
refresh navmesh and update the render instance transform, disembarkLift.
Graph links alone are not actor traversal. See docs/building-runtime-circulation.md.


## Runtime navigation and elevator owner

Use BuildingRuntime.h for native game clients. Generate the building recipe,
spawn static mesh/colliders, then attach BuildingCirculationScene with
independently allocated dynamic collider IDs. Rebuild existing nav::Mesh from
its authoritative collision-backed Source before scene.rebuildNavigation().
One BuildingRouteExecutor per actor receives Walk/Stair/CallLift/Wait/Board/
Ride/Exit commands; game ECS motor must acknowledge physical movements.
After world tick, apply only scene.update().publishedPlatforms and
publishedRiders. Never apply uncommitted attempted motions.
Navigation/physics owner integration is present at API level but untested,
not a completed gameplay elevator demo.


Stage 17: BuildingLiftGameSession<Body> composes existing ECS World,
SpatialWorld, NavMesh and authenticated lift state frames. The owner must
supply Entity authorization, unique platform and physical door collider IDs,
then advance exclusively in authoritative tick order. Actor boarding checks
actual cabin position and landing-door state; exit is permitted only at
an open landing. releaseAtLanding and detachIfVacant prevent casual unload
while passengers are riding. No game mode is wired or tested yet.


## Stage 17: Lift presentation from the authoritative scene

CPU models and renderer commands are owned by
engine/buildings/BuildingLiftPresentation.h. The code uses the *existing*
Renderer3D render::Model and instance/node transform APIs; it does not open
a new GPU backend, build GLB files or regenerate meshes during simulation.

**Spawn phase**: after BuildingLiftGameSession<Body>::attach() and the
existing SpatialWorld collider/door registration, call
session.makeVisualAssets(style). This constructs one dedicated
render::Model for each lift platform and one reusable half-width door leaf
mesh per lift feature (shared by its two doors on every floor).
Models have runtimeMutable nodes and disable rigid merge. The renderer
resource owner uploads each model once; it retains GPU leases until the
usual completion/fence retirement. The building runtime does not hold any
GPU handles.

**Committed world/render phase**: BuildingLiftGameSession::advance() first
commits collision, then actor rider movement, then releases an authoritative
server snapshot. After success the renderer reads session.visualFrame().
It reports a world-position per platform and per door leaf, yaw quarter-turn,
fraction open and snapshot revision. Renderer3D applies those positions to
its own existing instances; no CPU geometry rebuild and no GPU upload on
each frame. On scene fault, visualFrame() is invalid and must not be
published as a new transform (render owner retains its last committed pose
until authoritative recovery).

Doors animate with phases Open=fully open, Closing moves 1->0,
Traveling=closed, Opening moves 0->1. Only current-floor landing doors
move; remote destination doors remain closed until arrival. Door visuals
are **not** authority for collision or boarding: BuildingLiftDoorOwner
decides physical collision and BuildingLiftGameSession gates passenger
commands on actual landing-door state.

Minimal owner-phase usage (existing renderer API deliberately omitted):

    auto assets = session.makeVisualAssets(registeredStyle);
    // rendererOwner.upload/lease assets.platforms and assets.leaves exactly once
    auto tick = session.advance(ecs, spatial, &navmesh, dtMs, serverTick);
    if (tick.complete) {
        const auto visual = session.visualFrame();
        if (visual.valid) {
            // rendererOwner applies each platform/leaf worldPosition + yaw
            // to existing scene instances; no mesh regeneration.
        }
        // send tick.frame only via existing authenticated snapshot transport
    }

Cross-system invariants:
- graphics never updates authority or mutates a SpatialWorld collider.
- the game only publishes a new pose after physics+ECS commit succeeds.
- a lift platform is NOT part of the static runtime building Model; it
  is a dedicated instance, moved by its pose.
- clients can interpolate received server poses for rendering but never
  infer lift boarding/permissions from those interpolated positions.
- model lifetimes and GPU retirement belong to Renderer3D, not this API.

This is the source/presentation API boundary, not proof of a running
Metal screenshot or completed GPU scene integration. Tests were deferred
by the user; no build, CTest, Studio QA or GPU render executed for Stage 17.


## Authenticated lift commands — authoritative game integration

The existing game server receives an authenticated connection identity
separately from a lift command's bytes. Never trust a client-supplied
actor or scene ID without checking it against the authenticated session.

    #include "engine/buildings/BuildingCirculationEcsScene.h"

    // On authoritative world owner after attach(...) and attachLandingDoors(...):
    scene.configureAuthenticatedCommands(
        [&](uint64_t clientId, Entity actor) {
            return gameSession.ownsActor(clientId, actor);
        },
        [&](Entity actor, uint32_t feature, uint32_t floor) {
            return movementOwner.actorAtLiftLanding(actor, feature, floor);
        });

    // Existing authenticated network channel supplies clientId out-of-band.
    // On world owner thread, not inside a browser/renderer callback:
    auto accepted = scene.handleAuthenticatedPacket(clientId, packetBytes);

The request codec is BuildingLiftCommandWire.h (fixed 44-byte packet).
It is neither a network listener nor an authentication mechanism. It only
validates the payload and passes it to the persistent server command
authority, which owns monotonic per-actor sequence protection. ECS actors
still require physical door/boarding checks and the existing authority's
call, board, destination, exit state machine.

For outbound server snapshots use captureServerSnapshot(serverTick, out)
after successful owner-phase collision + ECS rider updates. Clients may
accept only trusted server state frames through BuildingLiftReplica.
