#pragma once
// Adapters from the generated optional `sports` settings section (milli-units, milli-turns, ticks) to the match
// rules and the positioning bot config.
#include "../gameplay/Configuration.h"
#include "Sports.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline SportsRulesConfig sportsRulesConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.sports;SportsRulesConfig c;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};
    c.periods=uint32_t(p.periods);c.periodTicks=uint32_t(p.periodTicks);c.overtimeTicks=uint32_t(p.overtimeTicks);c.restartDelayTicks=uint32_t(p.restartDelayTicks);
    c.maxStoppageTicks=uint32_t(p.maxStoppageTicks);c.clock=SportsClockMode(p.clockMode);c.swapEnds=p.swapEnds;c.goldenGoal=p.goldenGoal;
    c.pickupRadius=m(p.pickupRadiusMilli);c.controlSpeed=m(p.controlSpeedMilli);c.friction=m(p.frictionMilli);c.tackleRange=m(p.tackleRangeMilli);
    c.tackleBaseMilli=p.tackleBaseMilli;c.interceptMilli=p.interceptMilli;c.behindFoulMilli=p.behindFoulMilli;c.failFoulMilli=p.failFoulMilli;
    c.tackleCooldownTicks=uint32_t(p.tackleCooldownTicks);c.possessionLockTicks=uint32_t(p.possessionLockTicks);c.kickLockTicks=uint32_t(p.kickLockTicks);
    c.passSpeedMin=m(p.passSpeedMinMilli);c.passSpeedMax=m(p.passSpeedMaxMilli);c.shotSpeedMin=m(p.shotSpeedMinMilli);c.shotSpeedMax=m(p.shotSpeedMaxMilli);
    c.passError=m(p.passErrorMilli);c.shotError=m(p.shotErrorMilli);return c;
}
inline std::vector<std::string> sportsSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.sports;
    if(p.clockMode==2&&p.maxStoppageTicks==0)out.push_back("sports.maxStoppageTicks: must be positive with clockMode 2 (added stoppage)");
    if(p.possessionLockTicks>p.periodTicks)out.push_back("sports.possessionLockTicks: must not exceed periodTicks");
    if(!valid(sportsRulesConfig(settings)))out.push_back("sports: rules out of range");
    return out;
}
}
namespace aiverse::lib::bots {
inline SportsBotConfig sportsBotConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.sports;SportsBotConfig c;c.shootRange=Fixed::fromRatio(p.botShootRangeMilli,1000);c.tackleMilli=p.botTackleMilli;
    c.reactionTicks=uint32_t(p.botReactionTicks);c.decisionTicks=uint32_t(p.botDecisionTicks);return c;
}
}
