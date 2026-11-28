#pragma once
#include "MapDoc.h"
#include "engine/game/MapGrid.h"
#include <cmath>

namespace aiverse::editor {

// Both the full editor view and a cropped placement preview sample the same authored cells.
inline void copyPreviewCell(const MapData& d, const game::Rules& rules, size_t source,
                            game::MapGrid& grid, size_t target) {
    grid.flags[target] = game::cellFlagsFromChar(d.cells[source]);
    // diggable rock (rules "diggable"): solid cells drawn as rock blocks, like the client does
    const int dk = rules.findDiggable(d.cells[source]);
    grid.dig[target] = dk >= 0 ? uint8_t(dk + 1) : 0;
    grid.digHp[target] = dk >= 0 ? rules.diggables[size_t(dk)].hardness : Fixed{};
    if (dk >= 0) grid.flags[target] = 0;
    grid.height[target] = d.heights[source];
    grid.level[target] = d.levels[source];
    grid.region[target] = d.regions[source] == '.' ? int16_t(-1) : int16_t(d.regions[source]);
    int8_t fr = d.fieldRes[source];
    grid.fieldRes[target] = fr >= 0 ? int16_t(std::max(0, rules.findResource(d.fieldResources[size_t(fr)]))) : int16_t(-1);
    grid.fieldAmt[target] = Fixed::fromInt(d.fieldAmt[source]);
}

inline float snapPlacementCenter(float position, int footprint) {
    return footprint % 2 ? std::floor(position) + 0.5f : std::round(position);
}

} // namespace aiverse::editor
