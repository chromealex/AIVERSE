// CROWD -> IMPRINT automatic trace producer for moving agents.
// No parallel movement state: CROWD Agent is canonical. Hosts using their
// own movement system may call imprint::sampleMotion directly instead.
#pragma once
#include "CrowdEcs.h"
#include "ImprintTrails.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace aiverse::lib::worldprocess {
inline bool sampleCrowdFootprints(World& world,std::uint32_t tick){
    const World& read=world;
    std::vector<std::pair<Entity,imprint::Tracker>> trackers;
    read.eachExistingConst<imprint::Tracker>(
        [&](Entity entity,const imprint::Tracker& tracker){
            // Trackers may also be driven by Box3D, ActorMotor or game
            // scripting. This automatic adapter only owns CROWD sources.
            if(read.alive(entity)&&tracker.enabled&&
               read.tryGetConst<crowd::Agent>(tracker.source))
                trackers.push_back({entity,tracker});
        });
    std::sort(trackers.begin(),trackers.end(),
        [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    // Validate all CROWD sources before mutating any tracker.
    for(const auto& [entity,t]:trackers){
        (void)entity;
        const auto* a=read.tryGetConst<crowd::Agent>(t.source);
        if(!imprint::valid(t)||!a||!a->enabled||!crowd::valid(*a)||
           a->sampledTick!=tick||
           t.emittedTick==tick||
           (t.motionTick!=UINT32_MAX&&tick<t.motionTick))
            return false;
    }
    for(const auto& [entity,t]:trackers){
        const auto& agent=*read.tryGetConst<crowd::Agent>(t.source);
        if(!imprint::sampleMotion(world,entity,tick,agent.x,agent.z))
            return false;
    }
    return true;
}
inline void installCrowdFootprints(Simulation& sim,
                                  std::string name="imprint.crowd_sampling"){
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)sampleCrowdFootprints(s.world(),s.tick());
    });
}
} // namespace aiverse::lib::worldprocess
