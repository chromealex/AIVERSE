#pragma once
// physical-interactions: tuning shared by all interaction policies (see InteractionConfigured.h for the
// generated `interaction` settings section). Units: metres, kilograms, seconds, newtons, ticks.
#include "PhysicalCommon.h"
namespace aiverse::lib::genre {
struct InteractionConfig {
    Fixed gravity=Fixed::fromRatio(981,100),skin=Fixed::fromRatio(16,1000);
    // Carry: mass limit, pickup reach from the hold anchor (feet + holdHeight), hold point in front of the
    // carrier, follow speed of the held object, drop when held this far from the hold point while blocked.
    Fixed maxMass=30_fx,reach=2_fx,holdDistance=Fixed::fromRatio(6,5),holdHeight=Fixed::fromRatio(6,5),followSpeed=12_fx,breakDistance=1_fx;
    int64_t speedPenaltyMilli=15,minSpeedMilli=300; // carrier speed milli = 1000 - mass(kg)*penalty, at least minSpeedMilli
    // Throw: impulse (N*s) grows linearly from min to max over chargeTicks; flights end after maxFlightTicks.
    Fixed throwMinImpulse=20_fx,throwMaxImpulse=120_fx;uint32_t chargeTicks=45,maxFlightTicks=600;
    // Push/pull: free-mode speed cap; grid mode moves one cell in gridMoveTicks.
    Fixed pushMaxSpeed=Fixed::fromRatio(3,2);uint32_t gridMoveTicks=20;
    // Ropes: fixed XPBD iteration count per tick and per-tick velocity damping (milli).
    uint32_t ropeIterations=8;int64_t ropeDampingMilli=10;
};
inline std::string interactionConfigError(const InteractionConfig& c){
    auto in=[](Fixed v,Fixed lo,Fixed hi){return v>=lo&&v<=hi;};
    if(!in(c.gravity,Fixed{},1000_fx))return "gravity: must be 0..1000";
    if(!in(c.skin,Fixed::fromRaw(1),Fixed::fromRatio(1,4)))return "skin: must be in (0,0.25]";
    if(!in(c.maxMass,Fixed::fromRaw(1),kPhysMaxMass))return "maxMass: must be in (0,100000]";
    for(auto v:{c.reach,c.holdDistance,c.holdHeight,c.breakDistance})if(!in(v,Fixed{},100_fx))return "reach: distances must be 0..100";
    if(!in(c.followSpeed,Fixed::fromRaw(1),kPhysMaxSpeed))return "followSpeed: must be in (0,1000]";
    if(c.speedPenaltyMilli<0||c.speedPenaltyMilli>1000)return "speedPenaltyMilli: must be 0..1000";
    if(c.minSpeedMilli<0||c.minSpeedMilli>1000)return "minSpeedMilli: must be 0..1000";
    if(!in(c.throwMinImpulse,Fixed{},kPhysMaxForce)||c.throwMaxImpulse<c.throwMinImpulse||c.throwMaxImpulse>kPhysMaxForce)return "throwMinImpulse: must be 0..throwMaxImpulse";
    if(c.chargeTicks<1||c.chargeTicks>kPhysMaxPhaseTicks)return "chargeTicks: must be 1..3600";
    if(c.maxFlightTicks<1||c.maxFlightTicks>kPhysMaxTicks)return "maxFlightTicks: must be 1..1000000";
    if(!in(c.pushMaxSpeed,Fixed::fromRaw(1),100_fx))return "pushMaxSpeed: must be in (0,100]";
    if(c.gridMoveTicks<1||c.gridMoveTicks>kPhysMaxPhaseTicks)return "gridMoveTicks: must be 1..3600";
    if(c.ropeIterations<1||c.ropeIterations>64)return "ropeIterations: must be 1..64";
    if(c.ropeDampingMilli<0||c.ropeDampingMilli>1000)return "ropeDampingMilli: must be 0..1000";
    return {};
}
inline bool valid(const InteractionConfig& c){return interactionConfigError(c).empty();}
}
