#pragma once
// Action-level route execution for AI, player interaction and automated
// inhabitants. This portable state machine does NOT move an actor, operate
// a renderer, or mutate ECS/PHYSIS. Owners execute Walk/TraverseStairs and
// report arrival; elevator commands target the BuildingLiftController.
// No implicit teleport when a vertical room edge appears in the graph.
#include "BuildingCirculationRuntime.h"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::systems {

enum class BuildingRoutePhase : uint8_t {
    Idle, Walk, TraverseStairs, CallLift, WaitLift, BoardLift,
    SelectDestination, RidingLift, DisembarkLift, Completed, Blocked
};
struct BuildingRouteCommand {
    BuildingRoutePhase phase=BuildingRoutePhase::Idle;
    std::string edgeId,fromRoom,toRoom;
    uint32_t liftFeature=UINT32_MAX,fromFloor=0,toFloor=0;
    // The owning actor system must acknowledge Walk/TraverseStairs only
    // after collision-checked movement has actually completed.
    bool requiresMovementAck=false;
    bool terminal=false;
    std::string error;
};

class BuildingRouteExecutor {
public:
    void reset(){
        route_={};phase_=BuildingRoutePhase::Idle;
        index_=0;liftFeature_=UINT32_MAX;fromFloor_=toFloor_=0;
        error_.clear();boarded_=false;
    }
    bool begin(const BuildingRoomRoute& route,
               const BuildingStructureGraph& graph,
               const std::string& currentRoom){
        reset();
        if(!route.reachable||currentRoom.empty()||!graph){
            block("Route is not reachable or building graph is invalid");
            return false;
        }
        std::string expected=currentRoom;
        for(const auto& step:route.steps){
            if(step.fromRoom!=expected){
                block("Route edge is not continuous: "+step.edgeId);
                return false;
            }
            if(step.action==BuildingRouteAction::RequestLift){
                const auto edge=findEdge(graph,step.edgeId);
                if(!edge||edge->kind!=BuildingConnectionKind::Elevator||
                   !parseLiftFeature(edge->id,graph,liftFeature_)){
                    block("Route references an invalid lift connection");
                    return false;
                }
            }else {
                const auto edge=findEdge(graph,step.edgeId);
                if(!edge)return block("Route references a missing connection");
                if(step.action==BuildingRouteAction::ClimbStairs&&
                   edge->kind!=BuildingConnectionKind::Stair)
                    return block("Route has a non-stair stair action");
                if(step.action==BuildingRouteAction::WalkThrough&&
                   edge->kind!=BuildingConnectionKind::Passage)
                    return block("Route has a non-passage walk action");
            }
            expected=step.toRoom;
        }
        route_=route;index_=0;
        phase_=route_.steps.empty()?BuildingRoutePhase::Completed:
            phaseForStep(route_.steps.front());
        return true;
    }

    const BuildingRoomRoute& route()const noexcept{return route_;}
    BuildingRoutePhase phase()const noexcept{return phase_;}
    size_t stepIndex()const noexcept{return index_;}
    const std::string& error()const noexcept{return error_;}
    bool finished()const noexcept{return phase_==BuildingRoutePhase::Completed;}
    bool blocked()const noexcept{return phase_==BuildingRoutePhase::Blocked;}

    BuildingRouteCommand command(const BuildingStructureGraph& graph){
        BuildingRouteCommand out;out.phase=phase_;
        out.terminal=finished()||blocked();
        out.error=error_;
        if(out.terminal||phase_==BuildingRoutePhase::Idle)return out;
        const auto& step=route_.steps[index_];
        out.edgeId=step.edgeId;
        out.fromRoom=step.fromRoom;
        out.toRoom=step.toRoom;
        out.requiresMovementAck=phase_==BuildingRoutePhase::Walk||
                                phase_==BuildingRoutePhase::TraverseStairs;
        if(step.action==BuildingRouteAction::RequestLift) {
            const auto* edge=findEdge(graph,step.edgeId);
            const auto* from=edge?room(graph,step.fromRoom):nullptr;
            const auto* to=edge?room(graph,step.toRoom):nullptr;
            uint32_t feature=UINT32_MAX;
            if(!from||!to||!parseLiftFeature(step.edgeId,graph,feature)){
                block("Lift edge or floor metadata is invalid");
                out.phase=phase_;out.error=error_;out.terminal=true;
                return out;
            }
            out.liftFeature=feature;
            out.fromFloor=from->floor;
            out.toFloor=to->floor;
            liftFeature_=feature;fromFloor_=from->floor;toFloor_=to->floor;
        }
        return out;
    }

    // The mover acknowledges an actual physically reached room/edge.
    // This NEVER accepts stairs based solely on a graph connection.
    bool acknowledgeMovement(const std::string& reachedRoom){
        if(phase_!=BuildingRoutePhase::Walk&&
           phase_!=BuildingRoutePhase::TraverseStairs)return false;
        const auto& step=route_.steps[index_];
        if(reachedRoom!=step.toRoom)return false;
        nextStep();
        return true;
    }

    // Route owner calls pump at its authoritative AI update phase. Return
    // state changes only; caller drives lift controller through the existing
    // host and receives platform collision/GPU pose publication elsewhere.
    bool pump(const BuildingStructureGraph& graph,
              std::map<uint32_t,BuildingLiftController>& lifts){
        if(index_>=route_.steps.size()||
           phase_==BuildingRoutePhase::Blocked||
           phase_==BuildingRoutePhase::Completed||
           phase_==BuildingRoutePhase::Idle)return false;
        const auto cmd=command(graph);
        if(cmd.terminal)return false;
        if(cmd.liftFeature==UINT32_MAX)return false;
        auto found=lifts.find(cmd.liftFeature);
        if(found==lifts.end()||!found->second.online()){
            block("Lift is missing or offline");
            return true;
        }
        auto& lift=found->second;
        switch(phase_){
          case BuildingRoutePhase::CallLift:
            if(lift.readyAt(cmd.fromFloor))
                phase_=BuildingRoutePhase::BoardLift;
            else if(lift.call(cmd.fromFloor))
                phase_=BuildingRoutePhase::WaitLift;
            else return false; // occupied cabin: wait until it can be called
            return true;
          case BuildingRoutePhase::WaitLift:
            if(lift.readyAt(cmd.fromFloor)){
                phase_=BuildingRoutePhase::BoardLift;
                return true;
            }
            return false;
          case BuildingRoutePhase::BoardLift:
            // Reaches BoardLift only after an owner-confirmed physical
            // approach to the cabin. Boarding must be acknowledged
            // externally; room graph access is not spatial proof.
            return false;
          case BuildingRoutePhase::SelectDestination:
            if(lift.requestDestination(cmd.toFloor)){
                phase_=BuildingRoutePhase::RidingLift;
                return true;
            }
            return false;
          case BuildingRoutePhase::RidingLift:
            if(lift.readyAt(cmd.toFloor)){
                phase_=BuildingRoutePhase::DisembarkLift;
                return true;
            }
            return false;
          default:return false;
        }
    }

    // Host must call only once a gameplay actor has reached the real lift
    // boarding volume, and only at the pickup floor with open doors.
    bool acknowledgeBoarded(BuildingLiftController& lift){
        if(phase_!=BuildingRoutePhase::BoardLift||
           lift.snapshot().featureIndex!=liftFeature_||
           !lift.board(fromFloor_))return false;
        boarded_=true;
        phase_=BuildingRoutePhase::SelectDestination;
        return true;
    }
    // Host validates actor has physically exited onto the destination
    // floor before calling; this is not an automatic teleport.
    bool acknowledgeDisembarked(BuildingLiftController& lift,
                                const std::string& actualRoom){
        if(phase_!=BuildingRoutePhase::DisembarkLift||
           !boarded_||index_>=route_.steps.size()||
           actualRoom!=route_.steps[index_].toRoom||
           lift.snapshot().featureIndex!=liftFeature_||
           !lift.disembark(toFloor_))return false;
        boarded_=false;
        nextStep();
        return true;
    }
    // Cancellation releases any rider reservation ONLY when doors are
    // already open at a floor. Do not drop a rider during motion.
    bool cancel(BuildingLiftController* lift=nullptr){
        if(boarded_){
            if(!lift||lift->snapshot().featureIndex!=liftFeature_||
               !lift->snapshot().doorsOpen()||
               !lift->disembark(lift->snapshot().currentFloor))return false;
            boarded_=false;
        }
        reset();
        return true;
    }
private:
    static const BuildingGraphRoom* room(const BuildingStructureGraph& g,
                                          const std::string& id){
        for(const auto& r:g.rooms)if(r.id==id)return &r;
        return nullptr;
    }
    static const BuildingGraphConnection* findEdge(
        const BuildingStructureGraph& graph,const std::string& id){
        for(const auto& edge:graph.connections)if(edge.id==id)return &edge;
        return nullptr;
    }
    static bool parseLiftFeature(const std::string& id,
                                 const BuildingStructureGraph& graph,
                                 uint32_t& feature){
        // Avoid parsing arbitrary network-provided strings into actions:
        // a valid vertical edge must match a generated lift socket ID.
        if(id.rfind("vertical/",0)!=0)return false;
        const size_t slash=id.find('/',9);
        if(slash==std::string::npos)return false;
        const auto token=id.substr(9,slash-9);
        if(token.empty()||token.size()>7)return false;
        uint32_t value=0;
        for(char c:token){
            if(c<'0'||c>'9')return false;
            value=value*10u+uint32_t(c-'0');
        }
        const std::string socketId="lift/"+id.substr(9);
        bool found=false;
        for(const auto& socket:graph.sockets)
            if(socket.id==socketId&&socket.kind==BuildingFeatureKind::Elevator)
                found=true;
        if(!found)return false;
        feature=value;return true;
    }
    static BuildingRoutePhase phaseForStep(const BuildingRouteStep& step){
        if(step.action==BuildingRouteAction::ClimbStairs)
            return BuildingRoutePhase::TraverseStairs;
        if(step.action==BuildingRouteAction::RequestLift)
            return BuildingRoutePhase::CallLift;
        return BuildingRoutePhase::Walk;
    }
    void nextStep(){
        ++index_;liftFeature_=UINT32_MAX;
        if(index_>=route_.steps.size())
            phase_=BuildingRoutePhase::Completed;
        else phase_=phaseForStep(route_.steps[index_]);
    }
    bool block(std::string why){
        phase_=BuildingRoutePhase::Blocked;
        error_=std::move(why);return false;
    }
    BuildingRoomRoute route_;
    BuildingRoutePhase phase_=BuildingRoutePhase::Idle;
    size_t index_=0;
    uint32_t liftFeature_=UINT32_MAX,fromFloor_=0,toFloor_=0;
    bool boarded_=false;
    std::string error_;
};
} // namespace aiverse::lib::systems
