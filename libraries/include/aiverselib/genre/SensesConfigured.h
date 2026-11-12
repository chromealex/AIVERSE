#pragma once
// Adapters from the generated `senses` settings section (integer ticks, milli-units, milli-turns) to SensesConfig.
#include "../gameplay/Configuration.h"
#include "Senses.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline SensesConfig sensesConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.senses;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};SensesConfig c;
    c.sight={m(p.sightNearMilli),m(p.sightRangeMilli),m(p.sightHalfAngleMilli),m(p.peripheralRangeMilli),m(p.peripheralHalfAngleMilli),
        p.sightNearStrengthMilli,p.sightFarStrengthMilli,p.peripheralStrengthMilli,p.sightMinStrengthMilli,uint32_t(p.sightQueryBudget)};
    c.hearing={p.hearingThresholdMilli,uint32_t(p.noiseBudget),uint32_t(p.occlusionBudget),m(p.noiseMaxRadiusMilli)};
    c.trail={uint32_t(p.trailLifetimeTicks),m(p.trailRangeMilli),uint32_t(p.trailMaxMarkers),p.trailMinStrengthMilli};
    c.suspicion={p.suspicionMaxMilli,p.suspiciousAtMilli,p.searchingAtMilli,p.alertAtMilli,p.sightGainMilli,p.hearingGainMilli,p.trailGainMilli,
        p.suspicionDecayMilli,uint32_t(p.suspicionDecayDelayTicks)};
    c.alert={m(p.alertRadiusMilli),uint32_t(p.alertDelayTicks),p.alertReceivedMilli,uint32_t(p.alertMaxPending),p.alertRelay};
    c.search={uint32_t(p.searchGiveUpTicks),m(p.searchPointRadiusMilli),m(p.searchCandidateRadiusMilli),uint32_t(p.searchMaxPoints)};
    c.hiding={m(p.hideEnterRangeMilli)};
    return c;
}
// Cross-field rules the generated loader cannot express (strict threshold order); empty when valid.
inline std::vector<std::string> sensesSettingsErrors(const gameplay::GameplaySettings& settings){
    const auto& p=settings.senses;std::vector<std::string> out;
    if(p.suspiciousAtMilli>=p.searchingAtMilli)out.push_back("senses.suspiciousAtMilli: must be below searchingAtMilli");
    if(p.searchingAtMilli>=p.alertAtMilli)out.push_back("senses.searchingAtMilli: must be below alertAtMilli");
    if(p.alertAtMilli>p.suspicionMaxMilli)out.push_back("senses.alertAtMilli: must not exceed suspicionMaxMilli");
    if(out.empty())if(auto e=sensesConfigError(sensesConfig(settings));!e.empty())out.push_back("senses: "+e);
    return out;
}
}
