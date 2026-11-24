// Read-only legacy editor waterNetwork -> NEREID import bridge.
// This adapter is allowed to depend on RTS/editor-time RtsWater; the public
// NEREID component and sampling library are genre-independent.
// It does NOT edit, rebake or replace map cells, shores or waterNetwork.
#pragma once
#include "GameWater.h"
#include <cmath>
#include "engine/world/nereid/NereidRasterImport.h"

namespace aiverse::game::water {

// Use EXACTLY the same legacy rasterizer as the editor's water bake.
// The caller provides NEREID's Fixed cellMeters explicitly; it must match
// legacyCellMeters semantically (e.g. 4_fx with legacy 4.0f). Passing the
// already-baked cell raster to waterBodiesFromRaster is preferred if maps
// may have been altered by bridges or other post-bake tools.
// Preferred for actual game simulation: read its canonical post-bake '~'
// cells, including later bridge/terrain edits. No waterNetwork re-rasterization.
// Unlike the editor source, the baked grid has no body-kind information.
inline std::vector<lib::nereid::WaterBodyComponent> importBakedWaterCellsToNereid(
        const std::vector<char>& cells,int width,int height,
        const lib::nereid::WaterRasterImportConfig& config,
        char waterCell='~') {
    if(width<=0 || height<=0 ||
       uint64_t(width)*uint64_t(height)!=uint64_t(cells.size()) ||
       cells.size()>4194304u)
        throw std::invalid_argument("legacy baked water grid dimensions invalid");
    std::vector<uint8_t> wet;
    wet.reserve(cells.size());
    for(char cell:cells)wet.push_back(uint8_t(cell==waterCell));
    return lib::nereid::waterBodiesFromRaster(wet,width,height,config);
}

inline std::vector<lib::nereid::WaterBodyComponent> importWaterNetworkToNereid(
        const Network& network,int width,int height,float legacyCellMeters,
        const lib::nereid::WaterRasterImportConfig& config) {
    if(!std::isfinite(legacyCellMeters) || legacyCellMeters<=0.0f ||
       width<=0 || height<=0 ||
       uint64_t(width)*uint64_t(height)>4194304u)
        throw std::invalid_argument("legacy water raster dimensions or cellMeters invalid");
    if(std::fabs(double(legacyCellMeters)-config.cellMeters.toDouble())>
       1.0/double(Fixed::kOne))
        throw std::invalid_argument("NEREID/legacy water cellMeters mismatch");
    const Geometry geometry=buildGeometry(network,width,height,legacyCellMeters);
    const auto wet=waterCells(network,geometry,width,height,legacyCellMeters);
    return lib::nereid::waterBodiesFromRaster(wet,width,height,config);
}

} // namespace aiverse::game::water
