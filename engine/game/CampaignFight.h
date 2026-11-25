#pragma once
#include "libraries/include/aiverselib/mechanics/SensesPersistence.h"
#include "libraries/include/aiverselib/mechanics/WoundsDisease.h"

namespace aiverse::game {
struct CampaignFight {
    lib::mechanics::SensesState senses;
    lib::mechanics::HealthState health;
    Fixed lastHp{};
    int64_t penaltyMilli = 0;
    bool crouching = false;
    Fixed aimYaw{};
    Vec3 previousPosition{};
    bool shotNoisePending = false;
};

inline void io(Archive& a, CampaignFight& fight) {
    a(fight.senses, fight.health, fight.lastHp, fight.penaltyMilli, fight.crouching, fight.aimYaw,
      fight.previousPosition, fight.shotNoisePending);
}
}
