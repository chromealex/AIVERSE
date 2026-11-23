#pragma once
// Building-dependent graph snapshots over EXISTING AIVERSE graph APIs.
// They never mutate an ECS road network or a persisted ZoneState.
// Owner supplies physically committed access policy for each edge.
#include "engine/buildings/BuildingGraphChanges.h"
#include "engine/game/MapGrid.h"
#include <aiverselib/strategy/ZoneGraph.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

// Read-only filtered copy of the game's real road graph. A closed building
// gate can disable selected links without destroying source road entities.
// The original RoadGraph::path(World&,...) remains the path solver.
class BuildingRoadGraphView {
public:
    using CanTraverse=std::function<bool(uint16_t,uint16_t)>;
    bool initialize(const game::RoadGraph& source,CanTraverse policy,
                    std::string* error=nullptr){
        if(!policy||source.nodes.size()>UINT16_MAX||
           source.adj.size()!=source.nodes.size()){
            if(error)*error="Road graph topology or policy invalid";
            return false;
        }
        for(const auto& edges:source.adj)
            for(uint16_t target:edges)
                if(size_t(target)>=source.nodes.size()){
                    if(error)*error="Road adjacency references missing node";
                    return false;
                }
        source_=&source;
        policy_=std::move(policy);
        return rebuild(1,error);
    }
    // Only the owning game may update source RoadGraph topology. Rebuild
    // uses current source and committed collision/access state atomically.
    bool rebuild(uint64_t revision,std::string* error=nullptr){
        if(!source_||!policy_||revision==0)return false;
        game::RoadGraph next;
        next.nodes=source_->nodes;
        next.adj.resize(source_->adj.size());
        for(size_t i=0;i<source_->adj.size();++i)
            for(uint16_t j:source_->adj[i])
                if(policy_(static_cast<uint16_t>(i),j))
                    next.adj[i].push_back(j);
        filtered_=std::move(next);
        revision_=revision;
        if(error)error->clear();
        return true;
    }
    BuildingGraphChangeRouter::Consumer consumer(){
        return [this](const BuildingGraphChange& change){
            return change.sceneId!=0&&change.revision>0&&
                   rebuild(change.revision);
        };
    }
    const game::RoadGraph& graph()const noexcept{return filtered_;}
    uint64_t revision()const noexcept{return revision_;}
private:
    const game::RoadGraph* source_=nullptr; // existing owner retains lifetime
    CanTraverse policy_;
    game::RoadGraph filtered_;
    uint64_t revision_=0;
};

// ZoneGraph is directed and ability-gated. This view only removes door
// edges no longer physically accessible; never grants a new edge or
// modifies permanent ability grants, checkpoints or player ZoneState.
class BuildingZoneGraphView {
public:
    using CanTraverse=std::function<bool(const lib::strategy::ZoneDoor&)>;
    bool initialize(const lib::strategy::ZoneGraph& source,
                    CanTraverse policy,std::string* error=nullptr){
        if(!lib::strategy::valid(source)||!policy){
            if(error)*error="Invalid ZoneGraph or access policy";
            return false;
        }
        source_=&source;policy_=std::move(policy);
        return rebuild(1,error);
    }
    bool rebuild(uint64_t revision,std::string* error=nullptr){
        if(!source_||!policy_||revision==0||
           !lib::strategy::valid(*source_))return false;
        lib::strategy::ZoneGraph next;
        next.zones=source_->zones;
        for(const auto& door:source_->doors)
            if(policy_(door))next.doors.push_back(door);
        if(!lib::strategy::valid(next)){
            if(error)*error="Filtered ZoneGraph failed structural validation";
            return false;
        }
        filtered_=std::move(next);
        revision_=revision;
        if(error)error->clear();
        return true;
    }
    BuildingGraphChangeRouter::Consumer consumer(){
        return [this](const BuildingGraphChange& change){
            return change.sceneId!=0&&change.revision>0&&
                   rebuild(change.revision);
        };
    }
    const lib::strategy::ZoneGraph& graph()const noexcept{return filtered_;}
    uint64_t revision()const noexcept{return revision_;}
private:
    const lib::strategy::ZoneGraph* source_=nullptr;
    CanTraverse policy_;
    lib::strategy::ZoneGraph filtered_;
    uint64_t revision_=0;
};

// Railways is optional and not present at the expected include path in
// this feature branch. The REAL rail owner must validate current route /
// block reservations and accept invalidation before returning true.
// This callback never edits train reservations or bypasses stepRail().
inline BuildingGraphChangeRouter::Consumer buildingRailOwnerInvalidator(
    std::function<bool(const BuildingGraphChange&)> committedRailUpdate){
    return [committedRailUpdate=std::move(committedRailUpdate)](
        const BuildingGraphChange& change){
        return bool(committedRailUpdate)&&committedRailUpdate(change);
    };
}

} // namespace aiverse::building
