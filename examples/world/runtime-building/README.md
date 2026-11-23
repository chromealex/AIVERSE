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
