// PHYSIS Stage 18 — pure world-space geometry for editor/diagnostic overlays.
// All coordinates are authoritative Fixed metres. Renderers choose projection.
// No camera, ImGui, RHI, genre, Box3D, NEREID or Terra dependency.
#pragma once
#include "PhysisDebugPalette.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::physis {

struct SceneDiagnosticTile {
    std::int32_t cellX = 0, cellZ = 0;
    Vec3 corners[4]{};      // X/Z rectangle, Y = Cell.elevation + lift
    DebugColor color{};
};

inline std::vector<SceneDiagnosticTile> makeSceneDiagnosticTiles(
    const World& world, const DiagnosticRaster& raster,
    DebugPalette palette = DebugPalette::Thermal,
    Fixed lift = Fixed::fromRatio(1, 20)) {
    std::vector<SceneDiagnosticTile> result;
    const auto* state = world.resourceConst<State>();
    if (!state || raster.width <= 0 || raster.height <= 0 ||
        raster.stride <= 0 || raster.width > 256 || raster.height > 256 ||
        raster.rawValues.size() != raster.valid.size() ||
        raster.rawValues.size() != std::size_t(raster.width) * std::size_t(raster.height))
        return result;
    result.reserve(raster.sampleCount);
    const auto metres = std::int64_t(state->settings.cellSizeMeters);
    const auto span = metres * raster.stride;
    const auto maxMetres = std::numeric_limits<std::int64_t>::max() /
                           Fixed::kOne / 2;
    for (int row = 0; row < raster.height; ++row) {
        for (int col = 0; col < raster.width; ++col) {
            const std::size_t index = std::size_t(row) *
                                      std::size_t(raster.width) + std::size_t(col);
            if (!raster.valid[index]) continue;
            const auto xCell = std::int64_t(raster.originX) +
                               std::int64_t(col) * raster.stride;
            const auto zCell = std::int64_t(raster.originZ) +
                               std::int64_t(row) * raster.stride;
            if (xCell < std::numeric_limits<std::int32_t>::min() ||
                xCell > std::numeric_limits<std::int32_t>::max() ||
                zCell < std::numeric_limits<std::int32_t>::min() ||
                zCell > std::numeric_limits<std::int32_t>::max()) continue;
            const auto entity = cellAt(world, std::int32_t(xCell), std::int32_t(zCell));
            const auto* coordinate = world.tryGetConst<Cell>(entity);
            if (!coordinate) continue;
            const auto x0 = xCell * metres, z0 = zCell * metres;
            const auto x1 = x0 + span, z1 = z0 + span;
            if (x0 < -maxMetres || x0 > maxMetres ||
                x1 < -maxMetres || x1 > maxMetres ||
                z0 < -maxMetres || z0 > maxMetres ||
                z1 < -maxMetres || z1 > maxMetres) continue;
            SceneDiagnosticTile tile;
            tile.cellX = std::int32_t(xCell); tile.cellZ = std::int32_t(zCell);
            const Fixed y = coordinate->elevation + lift;
            tile.corners[0] = {Fixed::fromInt(x0), y, Fixed::fromInt(z0)};
            tile.corners[1] = {Fixed::fromInt(x1), y, Fixed::fromInt(z0)};
            tile.corners[2] = {Fixed::fromInt(x1), y, Fixed::fromInt(z1)};
            tile.corners[3] = {Fixed::fromInt(x0), y, Fixed::fromInt(z1)};
            tile.color = diagnosticColor(raster, index, palette);
            result.push_back(tile);
        }
    }
    return result;
}

} // namespace aiverse::physis
