// Editor-only TerraWeave exclusion policy for authored map content.
// Never used to mutate gameplay ECS or authoritative forest state.
#pragma once

#include "apps/editor/core/MapDoc.h"
#include "engine/world/TerraWeave.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::editor {

// Authoring space is always 100 cm per map cell. The procedural source may
// use a different sample spacing, so explicit unit conversion is mandatory.
inline int64_t terraWeaveWorldFromEditorCm(
        int64_t mapCm, int64_t worldOriginCm, int32_t generatorCellCm) {
    if (generatorCellCm <= 0 ||
        worldOriginCm < -terraweave::kWorldLimitCm ||
        worldOriginCm > terraweave::kWorldLimitCm ||
        mapCm < -terraweave::kWorldLimitCm ||
        mapCm > terraweave::kWorldLimitCm)
        throw std::out_of_range("TerraWeave editor/world coordinates");
    const int64_t worldCm = worldOriginCm + mapCm * generatorCellCm / 100;
    if (worldCm < -terraweave::kWorldLimitCm ||
        worldCm > terraweave::kWorldLimitCm)
        throw std::out_of_range("TerraWeave editor/world extent");
    return worldCm;
}

inline int64_t terraWeaveEditorFromWorldCm(
        int64_t worldCm, int64_t worldOriginCm, int32_t generatorCellCm) {
    if (generatorCellCm <= 0 ||
        worldCm < -terraweave::kWorldLimitCm ||
        worldCm > terraweave::kWorldLimitCm ||
        worldOriginCm < -terraweave::kWorldLimitCm ||
        worldOriginCm > terraweave::kWorldLimitCm)
        throw std::out_of_range("TerraWeave world/editor coordinates");
    return (worldCm - worldOriginCm) * 100 / generatorCellCm;
}

// Converts a sample in map-local centimetres into an allowed placement.
// Excludes water/roads/blocked cells, map entities and legacy road segments.
// RoadNetwork splines require a separate path-aware exclusion adapter.
inline bool terraWeavePreviewAllowsPlacement(
        const MapData& map, int64_t localXcm, int64_t localZcm,
        int32_t cellSizeCm = 100, int32_t entityClearanceCm = 175,
        int32_t roadClearanceCm = 225) {
    if (cellSizeCm <= 0 || entityClearanceCm < 0 || roadClearanceCm < 0)
        throw std::invalid_argument("TerraWeave preview placement settings");
    if (map.w <= 0 || map.h <= 0 ||
        map.cells.size() != size_t(map.w) * size_t(map.h)) return false;

    const int64_t cx = terraweave::detail::floorDiv(localXcm, cellSizeCm);
    const int64_t cz = terraweave::detail::floorDiv(localZcm, cellSizeCm);
    if (cx < 0 || cz < 0 || cx >= map.w || cz >= map.h) return false;

    const char cell = map.cells[size_t(cz) * size_t(map.w) + size_t(cx)];
    if (cell != '.' && cell != ',') return false;

    const auto docPosition = [cellSizeCm](const DocEntity& e, int64_t& x, int64_t& z) {
        if (!std::isfinite(e.x) || !std::isfinite(e.z) ||
            std::abs(double(e.x)) > 10000000.0 ||
            std::abs(double(e.z)) > 10000000.0) return false;
        x = int64_t(std::llround(double(e.x) * cellSizeCm));
        z = int64_t(std::llround(double(e.z) * cellSizeCm));
        return true;
    };
    const int64_t entityR = entityClearanceCm;
    const int64_t roadR = roadClearanceCm;
    for (const DocEntity& e : map.entities) {
        int64_t x = 0, z = 0;
        if (!docPosition(e, x, z)) continue;
        const int64_t dx = localXcm - x, dz = localZcm - z;
        if (dx >= -entityR && dx <= entityR &&
            dz >= -entityR && dz <= entityR) return false;
    }
    for (const auto& edge : map.roads) {
        if (edge.first < 0 || edge.second < 0 ||
            size_t(edge.first) >= map.entities.size() ||
            size_t(edge.second) >= map.entities.size()) continue;
        int64_t ax = 0, az = 0, bx = 0, bz = 0;
        if (!docPosition(map.entities[size_t(edge.first)], ax, az) ||
            !docPosition(map.entities[size_t(edge.second)], bx, bz)) continue;
        const int64_t vx = bx - ax, vz = bz - az;
        const int64_t wx = localXcm - ax, wz = localZcm - az;
        const long double length2 = static_cast<long double>(vx) * vx +
                                    static_cast<long double>(vz) * vz;
        if (length2 == 0) continue;
        const long double dot = static_cast<long double>(wx) * vx +
                                static_cast<long double>(wz) * vz;
        long double distance2;
        if (dot <= 0) {
            distance2 = static_cast<long double>(wx) * wx +
                        static_cast<long double>(wz) * wz;
        } else if (dot >= length2) {
            const int64_t dx = localXcm - bx, dz = localZcm - bz;
            distance2 = static_cast<long double>(dx) * dx +
                        static_cast<long double>(dz) * dz;
        } else {
            const long double cross = static_cast<long double>(wx) * vz -
                                      static_cast<long double>(wz) * vx;
            distance2 = cross * cross / length2;
        }
        if (distance2 <= static_cast<long double>(roadR) * roadR) return false;
    }
    return true;
}

} // namespace aiverse::editor
