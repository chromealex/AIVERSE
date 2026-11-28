#pragma once
#include "MapDoc.h"
#include "engine/game/GameCore.h"
#include <cmath>

namespace aiverse::editor {
inline void fillPreviewTerritory(const MapData& d, const game::Rules& rules, game::MapGrid& grid) {
    // starting territory (rules territory.startRadius around owned structures), so the scene shows the claimed floor
    std::fill(grid.territory.begin(), grid.territory.end(), uint8_t(0));
    if (const int r = rules.territory.startRadius; rules.territory.enabled && r > 0 && !d.planet())
        for (const auto& e : d.entities) {
            const int ti = rules.findType(e.type);
            if (e.owner <= 0 || e.owner > 8 || ti < 0 || !rules.types[size_t(ti)].structure) continue;
            const game::UnitType& t = rules.types[size_t(ti)];
            int x0, y0; game::GameCore::footprintAt(t, {Fixed::fromRatio(int64_t(std::lround(e.x * 1000)), 1000), Fixed{}, Fixed::fromRatio(int64_t(std::lround(e.z * 1000)), 1000)}, x0, y0);   // editor only: float -> fixed
            for (int y = y0 - r; y < y0 + t.footprintH + r; ++y)
                for (int x = x0 - r; x < x0 + t.footprintW + r; ++x) {
                    if (!grid.inside(x, y)) continue;
                    const size_t i = size_t(grid.index(x, y));
                    if ((grid.flags[i] & game::kCellGround) && !grid.territory[i]) grid.territory[i] = uint8_t(e.owner);
                }
        }
}
} // namespace aiverse::editor
