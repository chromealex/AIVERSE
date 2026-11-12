// Runtime state of authored routes, BW groups and the multiscreen view (RtsRoutes.cpp; data in RtsRoutesData.h).
#pragma once
#include <cstdint>
#include <vector>
#include "../../engine/core/ECS.h"
#include "RtsRoutesData.h"

namespace aiverse::game {

// A group being sent along a route (BW UnitsModule UnitGroup while it is not closed): spawns a row of up to
// `lines` units whenever the row in front has walked rowSpacing; the garrison pays per row.
struct SendGroup {
    uint32_t id = 0;
    Entity from = kNullEntity, to = kNullEntity;
    uint8_t owner = 0;
    int need = 0, spawned = 0;
    Fixed rowDist{};
    bool flying = false;
};

struct RouteState {
    bool active = false;                    // map routes / screen or rules lanes.groups: then saved and hashed
    std::vector<Entity> ends;               // 2 per route: current from / to garrison (re-resolved at the route ends)
    std::vector<int32_t> laneRoute;         // per GameState::lanes entry: the route it was built from
    std::vector<Fixed> laneScale;           // per lane: its length / length of line 0 (rows stay abreast, BW progress)
    std::vector<SendGroup> groups;          // open groups in creation order
    uint32_t signature = 0;                 // ends / blockers the lanes were built for
    Vec3 screenCenter{};                    // current screen center (BW -MultiscreenModule offset)
};

} // namespace aiverse::game
