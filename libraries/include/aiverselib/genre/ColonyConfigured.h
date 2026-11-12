#pragma once
// Adapter from the generated optional `colony` settings section to ColonyConfig (tick rate, mood bands, work speeds).
#include "../gameplay/Configuration.h"
#include "ColonyTypes.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline ColonyConfig colonyConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.colony;ColonyConfig c;
    c.ticksPerHour=p.ticksPerHour;c.moodBase=p.moodBase;c.breakAt=p.moodBreakAt;c.lowAt=p.moodLowAt;c.highAt=p.moodHighAt;c.breakTicks=p.breakTicks;
    c.lowSpeedMilli=p.lowSpeedMilli;c.contentSpeedMilli=p.contentSpeedMilli;c.highSpeedMilli=p.highSpeedMilli;c.maxResponseJobs=p.maxResponseJobs;return c;
}
// Cross-field rules the generated loader cannot express (strict band order); empty when valid.
inline std::vector<std::string> colonySettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.colony;
    if(!(p.moodBreakAt<p.moodLowAt&&p.moodLowAt<p.moodHighAt))out.push_back("colony.moodBreakAt: moodBreakAt < moodLowAt < moodHighAt required");
    return out;
}
}
