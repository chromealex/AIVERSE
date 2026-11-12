#pragma once
// Adapter from the generated optional `narrative` settings section (integer ticks) to NarrativeConfig.
#include "../gameplay/Configuration.h"
#include "NarrativeScenes.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline NarrativeConfig narrativeConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.narrative;NarrativeConfig c;
    c.autoAdvanceTicks=uint32_t(p.autoAdvanceTicks);c.skipLinesPerTick=uint32_t(p.skipLinesPerTick);c.backlogLimit=uint32_t(p.backlogLimit);
    c.barkCooldownTicks=uint32_t(p.barkCooldownTicks);c.speakerCooldownTicks=uint32_t(p.speakerCooldownTicks);
    c.maxPending=uint32_t(p.maxPendingConsequences);c.maxBarkRequests=uint32_t(p.maxBarkRequests);c.meter={p.meterMinimum,p.meterMaximum,p.meterInitial};
    return c;
}
// Empty when the section maps to a valid NarrativeConfig (the generated loader already checks ranges/relations).
inline std::vector<std::string> narrativeSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;if(auto e=narrativeConfigError(narrativeConfig(settings));!e.empty())out.push_back("narrative."+e);return out;
}
}
