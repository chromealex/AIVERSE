#pragma once
// Adapter from the generated optional `tactics` settings section to TacticsRules defaults; scenario JSON `rules`
// then overrides individual values (readTacticsScenario(json, tacticsRulesFromSettings(settings))).
#include "../gameplay/Configuration.h"
#include "TacticsTurns.h"
namespace aiverse::lib::genre {
inline TacticsRules tacticsRulesFromSettings(const gameplay::GameplaySettings& settings,TacticsRules base=TacticsRules{}){
    const auto& p=settings.tactics;auto& f=base.fire;
    base.roundLimit=int32_t(p.roundLimit);f.halfCoverMilli=int32_t(p.halfCoverMilli);f.fullCoverMilli=int32_t(p.fullCoverMilli);f.flankBonusMilli=int32_t(p.flankBonusMilli);
    f.rangePenaltyMilli=int32_t(p.rangePenaltyMilli);f.overwatchPenaltyMilli=int32_t(p.overwatchPenaltyMilli);f.minHitMilli=int32_t(p.minHitMilli);f.maxHitMilli=int32_t(p.maxHitMilli);
    f.coverCosMilli=int32_t(p.coverCosMilli);return base;
}
}
