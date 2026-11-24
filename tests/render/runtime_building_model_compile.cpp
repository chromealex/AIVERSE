// Compile-only contract: Renderer3D bridge is usable in a packaged game
// without linking Studio, editor or GLB export code.
#include "engine/buildings/RuntimeBuildingModel.h"
#include "engine/buildings/BuildingEcsObstruction.h"
#include "engine/buildings/BuildingEcsReconcile.h"
#include "engine/buildings/BuildingRenderer3DScene.h"
#include <memory>
#include <type_traits>

using aiverse::building::runtimeBuildingModel;
using aiverse::lib::systems::StyledStructure;

static_assert(std::is_same_v<
    decltype(runtimeBuildingModel(std::declval<const StyledStructure&>())),
    std::unique_ptr<aiverse::render::Model>>);

std::unique_ptr<aiverse::render::Model> generatedModelCpu(const StyledStructure& geometry) {
    // This is a CPU-only conversion. The game Renderer3D owner invokes
    // render::uploadModel(device, *model) on the GPU/device thread later.
    return runtimeBuildingModel(geometry);
}

static_assert(std::is_same_v<
    decltype(aiverse::building::registerBuildingEcsPersistence(
        std::declval<aiverse::Simulation&>(),std::declval<uint64_t>())),
    void>);
static_assert(std::is_same_v<
    decltype(aiverse::building::buildingEcsSweptOccupied(
        std::declval<const aiverse::World&>(),
        std::declval<const aiverse::world::Bounds&>(),
        std::declval<const aiverse::world::Bounds&>())),
    bool>);
static_assert(std::is_same_v<
    decltype(std::declval<aiverse::building::BuildingDualLeafOwner&>().attachRestored(
        std::declval<const std::vector<aiverse::building::BuildingAperturePose>&>(),
        std::declval<const std::map<std::string,aiverse::building::BuildingDualLeafIds>&>(),
        std::declval<const std::map<std::string,uint16_t>&>(),
        uint64_t{},std::declval<aiverse::world::SpatialWorld&>(),uint64_t{},
        std::declval<aiverse::building::BuildingDualLeafOwner::SweptObstruction>())),
    bool>);
static_assert(std::is_same_v<
    decltype(std::declval<aiverse::building::BuildingRenderer3DScene&>().draw(
        std::declval<aiverse::render::Renderer3D&>(),aiverse::world::Cell{},int{},uint32_t{})),
    bool>);
