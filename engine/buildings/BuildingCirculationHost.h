#pragma once
// Game-facing owner of building room routes, lift controllers and off-mesh
// links. Navigation still uses the existing nav::Mesh. The scene/physics
// owner consumes lift movement commands and publishes transformed dynamic
// colliders and render instance poses in its normal update phase.
#include "engine/buildings/BuildingNavMeshBridge.h"
#include <aiverselib/systems/BuildingRouteExecutor.h>
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {
struct BuildingCirculationTick {
    std::vector<BuildingLiftEvent> events;
    std::vector<BuildingLiftPlatformMotion> platformMovements;
    uint64_t graphRevision=0;
};
class BuildingCirculationHost {
public:
    bool initialize(const RuntimeBuildingResult& generated,
                    Vec3 origin,uint32_t yaw,
                    std::string* error=nullptr){
        auto fail=[&](const std::string& msg){
            if(error)*error=msg;
            return false;
        };
        if(!generated||!generated.structureGraph||yaw>3)
            return fail("Runtime-generated building has no valid circulation graph");
        BuildingStructureGraph staged=generated.structureGraph;
        std::map<uint32_t,BuildingLiftController> lifts;
        std::set<uint32_t> seen;
        for(const auto& socket:staged.sockets){
            if(socket.kind!=BuildingFeatureKind::Elevator ||
               !seen.insert(socket.featureIndex).second)continue;
            BuildingLiftController controller;
            std::string problem;
            if(!controller.initialize(staged,socket.featureIndex,&problem))
                return fail(problem);
            lifts.emplace(socket.featureIndex,std::move(controller));
        }
        const auto width=generated.building.constructionSize.width;
        const auto depth=generated.building.constructionSize.depth;
        if(!(width>0)||!(depth>0))
            return fail("Building has invalid local geometry dimensions");
        graph_=std::move(staged);
        lifts_=std::move(lifts);
        width_=width;depth_=depth;origin_=origin;yaw_=yaw;
        navLinks_={};
        ++graphRevision_;
        if(error)error->clear();
        return true;
    }

    void clear(){
        graph_={};lifts_.clear();navLinks_={};
        ++graphRevision_;
    }
    const BuildingStructureGraph& graph()const noexcept{return graph_;}
    const std::map<uint32_t,BuildingLiftController>& lifts()const noexcept{return lifts_;}
    const BuildingNavLinkBatch& navLinks()const noexcept{return navLinks_;}
    uint64_t graphRevision()const noexcept{return graphRevision_;}
    bool rebuildNavigation(const nav::Mesh& mesh,std::string* error=nullptr){
        auto candidate=resolveBuildingOffMeshLinks(
            graph_,mesh,width_,depth_,origin_,yaw_);
        if(!candidate) {
            navLinks_={};
            if(error)*error=candidate.error;
            return false;
        }
        navLinks_=std::move(candidate);
        if(error)error->clear();
        return true;
    }

    // First stage of navigation. Horizontal legs are verified against native
    // nav::Mesh A* corridors. Stairs/lifts are explicit off-mesh actions
    // resolved to real polygons; agent locomotion executes them afterwards.
    BuildingNavRoute route(const nav::Mesh& mesh,
                           const std::string& fromRoom,
                           const std::string& toRoom,
                           BuildingRoutePolicy policy={})const{
        return planBuildingNavRoute(graph_,mesh,navLinks_,fromRoom,toRoom,
                                    policy,&lifts_);
    }
    BuildingRoomRoute routeRooms(const std::string& start,const std::string& goal,
                                BuildingRoutePolicy policy={})const{
        return planBuildingRoomRoute(graph_,start,goal,policy,&lifts_);
    }
    // A caller owns one executor per actor. Route creation is based on the
    // current room graph and live lift availability, not on a fake navmesh
    // vertical portal. The movement system must acknowledge each real
    // staircase/corridor traversal and cabin boarding/disembarking.
    bool createRouteExecutor(BuildingRouteExecutor& executor,
                             const std::string& fromRoom,
                             const std::string& toRoom,
                             BuildingRoutePolicy policy={})const {
        const auto planned=routeRooms(fromRoom,toRoom,policy);
        return executor.begin(planned,graph_,fromRoom);
    }
    BuildingRouteCommand nextAgentCommand(BuildingRouteExecutor& executor){
        return executor.command(graph_);
    }
    bool pumpAgent(BuildingRouteExecutor& executor){
        return executor.pump(graph_,lifts_);
    }
    bool acknowledgeAgentBoarded(BuildingRouteExecutor& executor,
                                 uint32_t liftFeature){
        auto it=lifts_.find(liftFeature);
        return it!=lifts_.end()&&executor.acknowledgeBoarded(it->second);
    }
    bool acknowledgeAgentDisembarked(BuildingRouteExecutor& executor,
                                    uint32_t liftFeature,
                                    const std::string& actualRoom){
        auto it=lifts_.find(liftFeature);
        return it!=lifts_.end()&&
            executor.acknowledgeDisembarked(it->second,actualRoom);
    }
    bool cancelAgentRoute(BuildingRouteExecutor& executor,
                          uint32_t liftFeature=UINT32_MAX){
        auto it=lifts_.find(liftFeature);
        return executor.cancel(it==lifts_.end()?nullptr:&it->second);
    }
    bool callLift(uint32_t feature,uint32_t targetFloor){
        auto it=lifts_.find(feature);
        return it!=lifts_.end()&&it->second.call(targetFloor);
    }
    bool boardLift(uint32_t feature,uint32_t floor,uint32_t passengers=1){
        auto it=lifts_.find(feature);
        return it!=lifts_.end()&&it->second.board(floor,passengers);
    }
    bool requestLiftDestination(uint32_t feature,uint32_t targetFloor){
        auto it=lifts_.find(feature);
        return it!=lifts_.end()&&it->second.requestDestination(targetFloor);
    }
    bool disembarkLift(uint32_t feature,uint32_t floor,uint32_t passengers=1){
        auto it=lifts_.find(feature);
        return it!=lifts_.end()&&it->second.disembark(floor,passengers);
    }
    bool takeLiftOffline(uint32_t feature,bool offline){
        auto it=lifts_.find(feature);
        if(it==lifts_.end())return false;
        it->second.setOffline(offline);
        return true;
    }
    // Called in the authoritative world owner phase. Do NOT mutate
    // SpatialWorld, navmesh or GPU here; return explicit commands for their
    // owning phases. The originating building's static collision/Model
    // MUST have been generated with includeInitialLiftPlatform=false.
    BuildingCirculationTick advance(uint32_t elapsedMs){
        BuildingCirculationTick out;
        out.graphRevision=graphRevision_;
        for(auto& [feature,controller]:lifts_){
            (void)feature;
            controller.advance(elapsedMs,&out.events);
            if(auto movement=controller.takePlatformMotion())
                out.platformMovements.push_back(*movement);
        }
        return out;
    }

    // Replication: authoritative server supplies revisioned state; only
    // newer validated snapshots are accepted. Platform motion is published
    // next tick using the same owner-phase collision command.
    bool applyReplicatedLiftState(const BuildingLiftSnapshot& snapshot){
        auto it=lifts_.find(snapshot.featureIndex);
        return it!=lifts_.end()&&it->second.applyReplicatedSnapshot(snapshot);
    }

private:
    BuildingStructureGraph graph_;
    std::map<uint32_t,BuildingLiftController> lifts_;
    BuildingNavLinkBatch navLinks_;
    Vec3 origin_{};
    float width_=0,depth_=0;
    uint32_t yaw_=0;
    uint64_t graphRevision_=0;
};
} // namespace aiverse::building
