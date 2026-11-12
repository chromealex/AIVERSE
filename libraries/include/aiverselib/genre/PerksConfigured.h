#pragma once
// Adapters from the generated `perks` settings section (integer ticks, milli-metres) to the match-progression
// configs, plus cross-field checks the generated loader cannot express.
#include "../gameplay/Configuration.h"
#include "Perks.h"
#include "XpPickups.h"
#include "Summons.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline PerkConfig perkConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.perks;PerkConfig c;
    c.choices=uint32_t(p.offerChoices);c.rerolls=uint32_t(p.offerRerolls);c.rerollAvoidsRepeat=p.rerollAvoidsRepeat;c.maxLevel=uint32_t(p.maxLevel);
    c.xpGrowth=p.xpGrowthPerLevel;c.maxRecruitsPerSlot=uint32_t(p.maxRecruitsPerSlot);c.maxSquad=uint32_t(p.maxSquad);
    return c;
}
inline PickupConfig pickupConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.perks;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};PickupConfig c;
    c.collectRadius=m(p.pickupRadiusMilli);c.magnetRadius=m(p.pickupMagnetRadiusMilli);c.grabRadius=m(p.pickupGrabRadiusMilli);
    c.attractSpeed=m(p.pickupAttractSpeedMilli);c.attractAccel=m(p.pickupAttractAccelMilli);c.maxSpeed=m(p.pickupMaxSpeedMilli);
    c.lifetime=uint32_t(p.pickupLifetimeTicks);c.landing=uint32_t(p.pickupLandingTicks);c.maxOrbs=uint32_t(p.pickupMaxOrbs);
    return c;
}
inline SummonConfig summonConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.perks;SummonConfig c;
    c.progressTicks=uint32_t(p.summonProgressTicks);c.cooldownTicks=uint32_t(p.summonCooldownTicks);c.resetOnExit=p.summonResetOnExit;
    c.maxAllies=uint32_t(p.summonMaxAllies);c.charges=uint32_t(p.summonCharges);c.chargeRecharge=uint32_t(p.summonChargeRechargeTicks);
    return c;
}
// Empty when valid.
inline std::vector<std::string> perkSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.perks;
    for(auto [v,name]:{std::pair<int64_t,const char*>{p.pickupRadiusMilli,"pickupRadiusMilli"},{p.pickupMagnetRadiusMilli,"pickupMagnetRadiusMilli"},{p.pickupGrabRadiusMilli,"pickupGrabRadiusMilli"}})if(v>100000000)out.push_back(std::string("perks.")+name+": must be at most 100000000");
    if(p.pickupAttractSpeedMilli>0&&p.pickupGrabRadiusMilli==0)out.push_back("perks.pickupGrabRadiusMilli: must be positive when pickupAttractSpeedMilli > 0");
    if(p.pickupMaxSpeedMilli>1000000)out.push_back("perks.pickupMaxSpeedMilli: must be at most 1000000");
    if(p.summonChargeRechargeTicks>0&&p.summonCharges==0)out.push_back("perks.summonChargeRechargeTicks: requires summonCharges");
    if(out.empty()){
        if(auto e=perkConfigError(perkConfig(settings));!e.empty())out.push_back(e);
        if(auto e=pickupConfigError(pickupConfig(settings));!e.empty())out.push_back("perks."+e);
        if(auto e=summonConfigError(summonConfig(settings));!e.empty())out.push_back("perks."+e);
    }
    return out;
}
}
