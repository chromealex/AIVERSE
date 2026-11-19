// TerraWeave -> legacy AIVERSE heightfield corner grid conversion.
// Pure CPU adapter, independent of editor UI and rendering.
#pragma once
#include "engine/world/TerraWeave.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct LegacyHeightfieldOptions {
    int64_t worldOriginXcm = 0;
    int64_t worldOriginZcm = 0;
    int32_t cellSizeCm = 100; // existing RTS terrain units: one cell = one metre
};

inline std::vector<int16_t> generateLegacyCorners(
        const TerrainSettings& terrain,
        int widthCells, int heightCells,
        LegacyHeightfieldOptions options = {}) {
    if (widthCells <= 0 || heightCells <= 0 ||
        widthCells > 2048 || heightCells > 2048 || options.cellSizeCm <= 0)
        throw std::invalid_argument("TerraWeave legacy heightfield size");
    // Validate origins and map span before signed addition in the sample loop.
    const int64_t spanX = int64_t(widthCells) * options.cellSizeCm;
    const int64_t spanZ = int64_t(heightCells) * options.cellSizeCm;
    if (options.worldOriginXcm < -kWorldLimitCm ||
        options.worldOriginZcm < -kWorldLimitCm ||
        options.worldOriginXcm > kWorldLimitCm ||
        options.worldOriginZcm > kWorldLimitCm ||
        options.worldOriginXcm + spanX > kWorldLimitCm ||
        options.worldOriginZcm + spanZ > kWorldLimitCm)
        throw std::out_of_range("TerraWeave legacy map world extent");
    const size_t side = static_cast<size_t>(widthCells) + 1;
    std::vector<int16_t> corners(side * (static_cast<size_t>(heightCells) + 1));
    for (int z = 0; z <= heightCells; ++z)
        for (int x = 0; x <= widthCells; ++x) {
            const int64_t worldX = options.worldOriginXcm + int64_t(x) * options.cellSizeCm;
            const int64_t worldZ = options.worldOriginZcm + int64_t(z) * options.cellSizeCm;
            const int64_t heightCm = heightAtCm(terrain, worldX, worldZ);
            // Legacy MapData::corners use signed 1/16 of a map unit (metre).
            // Round-to-nearest in integer arithmetic; never silently clamp.
            const int64_t scaled = heightCm * 16;
            const int64_t corner = (scaled >= 0 ? scaled + 50 : scaled - 50) / 100;
            if (corner < std::numeric_limits<int16_t>::min() ||
                corner > std::numeric_limits<int16_t>::max())
                throw std::out_of_range("TerraWeave height outside legacy int16 range");
            corners[size_t(z) * side + x] = static_cast<int16_t>(corner);
        }
    return corners;
}

} // namespace aiverse::terraweave
