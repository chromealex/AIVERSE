#pragma once
// Adapters from the generated optional `cinematic` settings section (integer ticks, milli scales) to QTE options
// and the hit-stop / slow-motion time-scale presets.
#include "../gameplay/Configuration.h"
#include "CinematicTime.h"
#include "CinematicQte.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline QteOptions qteOptions(const gameplay::GameplaySettings& settings){
    const auto& p=settings.cinematic;return {p.qteWindowScaleMilli,p.qteAccessibleWindowMilli,p.qteMashScaleMilli,p.qteHoldScaleMilli,p.qteAutoSucceed};
}
inline TimeRequest hitStopRequest(const gameplay::GameplaySettings& settings,int64_t domain=kDomainWorld){
    const auto& p=settings.cinematic;return hitStop(domain,uint32_t(p.hitStopTicks),int32_t(p.hitStopPriority));
}
inline TimeRequest slowMotionRequest(const gameplay::GameplaySettings& settings,int64_t domain=kDomainWorld){
    const auto& p=settings.cinematic;return slowMotion(domain,{p.slowMotionNumerator,p.slowMotionDenominator},uint32_t(p.slowMotionTicks),int32_t(p.slowMotionPriority));
}
// Cross-field rules the generated loader cannot express; empty when valid.
inline std::vector<std::string> cinematicSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.cinematic;
    if(!timeUnits({p.slowMotionNumerator,p.slowMotionDenominator}))out.push_back("cinematic.slowMotionNumerator: ratio must be exact (denominator 1..16)");
    if(!valid(qteOptions(settings)))out.push_back("cinematic.qteWindowScaleMilli: QTE scales out of range");
    return out;
}
}
