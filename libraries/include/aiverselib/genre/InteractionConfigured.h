#pragma once
// Adapter from the generated optional `interaction` settings section (milli-units, ticks) to InteractionConfig.
#include "../gameplay/Configuration.h"
#include "InteractionConfig.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline InteractionConfig interactionConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.interaction;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};InteractionConfig c;
    c.gravity=m(p.gravityMilli);c.skin=m(p.skinMilli);
    c.maxMass=m(p.carryMaxMassMilli);c.reach=m(p.carryReachMilli);c.holdDistance=m(p.carryHoldDistanceMilli);c.holdHeight=m(p.carryHoldHeightMilli);
    c.followSpeed=m(p.carryFollowSpeedMilli);c.breakDistance=m(p.carryBreakDistanceMilli);c.speedPenaltyMilli=p.carrySpeedPenaltyMilli;c.minSpeedMilli=p.carryMinSpeedMilli;
    c.throwMinImpulse=m(p.throwMinImpulseMilli);c.throwMaxImpulse=m(p.throwMaxImpulseMilli);c.chargeTicks=uint32_t(p.throwChargeTicks);c.maxFlightTicks=uint32_t(p.flightMaxTicks);
    c.pushMaxSpeed=m(p.pushMaxSpeedMilli);c.gridMoveTicks=uint32_t(p.pushGridMoveTicks);c.ropeIterations=uint32_t(p.ropeIterations);c.ropeDampingMilli=p.ropeDampingMilli;
    return c;
}
// Cross-field rules beyond the generated loader; empty when valid.
inline std::vector<std::string> interactionSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.interaction;
    if(p.throwMinImpulseMilli>p.throwMaxImpulseMilli)out.push_back("interaction.throwMinImpulseMilli: must not exceed throwMaxImpulseMilli");
    if(out.empty())if(auto e=interactionConfigError(interactionConfig(settings));!e.empty())out.push_back("interaction."+e);
    return out;
}
}
