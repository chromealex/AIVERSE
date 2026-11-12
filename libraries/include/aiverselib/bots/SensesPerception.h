#pragma once
// Bridge stealth-senses perception (genre/Senses.h) into genre-bot input (bots/GenreCommon.h).
// Senses decides WHAT an observer knows (awareness, last known position, visible this tick); the owner adds
// only what the observer can actually see of a visible target (team, velocity, facing, guard, telegraphed attack).
// Pure function: no state, deterministic order (ascending target id), no RNG.
#include "GenreCommon.h"
#include "../genre/Senses.h"
#include <optional>
#include <span>
#include <vector>

namespace aiverse::lib::bots {

// Observable facts about one target, supplied by the authoritative owner.
struct TargetFacts {
    int64_t team=0;
    Vec3 position{},velocity{};   // current pose; used only while the target is visible this tick
    Fixed yaw{};
    bool guarding=false,vulnerable=false;
    int64_t healthMilli=1000;
    MeleeThreat threat{};
};

struct PerceptionFilter {
    // Minimum awareness for a remembered (not visible) target to stay in the bot's list at its last known position.
    genre::Awareness rememberFrom=genre::Awareness::Searching;
    // Minimum awareness for a visible target to be engageable (visible=true for the bot).
    genre::Awareness engageFrom=genre::Awareness::Alert;
};

// facts(targetId) -> std::optional<TargetFacts>; nullopt drops the target (despawned, dead, friendly, ...).
// Only records of `observer` are used; anonymous disturbances (target 0) are skipped: use searchGoal() for them.
template<class FactsFn>
std::vector<PerceivedTarget> perceivedFromSenses(std::span<const genre::PerceivedTarget> perceived,int64_t observer,
                                                 FactsFn&& facts,const PerceptionFilter& filter={}){
    std::vector<PerceivedTarget> out;
    for(const auto& p:perceived){
        if(p.observer!=observer||p.target<=0)continue;
        const bool engage=p.visibleNow&&p.level>=filter.engageFrom;
        if(!engage&&p.level<filter.rememberFrom)continue;
        const std::optional<TargetFacts> f=facts(p.target);
        if(!f)continue;
        PerceivedTarget t;
        t.id=p.target;t.team=f->team;t.visible=engage;
        if(engage){
            t.position=f->position;t.velocity=f->velocity;t.yaw=f->yaw;
            t.guarding=f->guarding;t.vulnerable=f->vulnerable;t.healthMilli=f->healthMilli;t.threat=f->threat;
        }else{
            t.position=p.lastKnown; // remembered: never shot at or locked (GenreCommon contract)
        }
        out.push_back(t);
    }
    std::sort(out.begin(),out.end(),[](const PerceivedTarget& a,const PerceivedTarget& b){return a.id<b.id;});
    return out;
}

} // namespace aiverse::lib::bots
