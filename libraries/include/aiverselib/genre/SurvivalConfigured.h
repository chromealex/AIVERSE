#pragma once
// Adapter from the generated optional `survival` settings section to SurvivalConfig (body model and weather chill/wetting).
#include "../gameplay/Configuration.h"
#include "Survival.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline SurvivalConfig survivalConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.survival;SurvivalConfig c;
    c.coreNormalMilli=p.coreNormalMilli;c.comfortLowMilli=p.comfortLowMilli;c.comfortHighMilli=p.comfortHighMilli;
    c.coolingMicro=p.coolingMicro;c.heatingMicro=p.heatingMicro;c.recoveryMicro=p.recoveryMicro;
    c.coldMilli=p.coldMilli;c.severeColdMilli=p.severeColdMilli;c.hotMilli=p.hotMilli;c.severeHotMilli=p.severeHotMilli;
    c.hungerPerTick=p.hungerPerTick;c.thirstPerTick=p.thirstPerTick;c.staminaDrain=p.staminaDrain;c.staminaRegen=p.staminaRegen;c.lowAt=p.lowAt;
    c.wetChillMilli=p.wetChillMilli;c.dryPerTick=p.dryPerTick;c.starveDamage=p.starveDamage;c.thirstDamage=p.thirstDamage;
    c.coldDamage=p.coldDamage;c.heatDamage=p.heatDamage;c.conditionRegen=p.conditionRegen;
    c.weather.windChillMilli=p.windChillMilli;c.weather.wetPerTick=p.wetPerTick;return c;
}
// Cross-field rules (strict temperature order around the normal core); empty when valid.
inline std::vector<std::string> survivalSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;
    if(auto e=survivalConfigError(survivalConfig(settings));!e.empty())out.push_back(e);
    return out;
}
}
