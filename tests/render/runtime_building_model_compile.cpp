// Compile-only contract: Renderer3D bridge is usable in a packaged game
// without linking Studio, editor or GLB export code.
#include "engine/buildings/RuntimeBuildingModel.h"
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
