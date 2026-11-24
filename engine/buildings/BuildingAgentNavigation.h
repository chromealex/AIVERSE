#pragma once
// ECS route intent + execution for any AI-controlled native entity.
// Uses existing building room/nav route solver, common authoritative
// command queues and the project's collision-checked movement owner.
// No fake movement/teleport, genre module, or new pathfinding algorithm.
#include "engine/buildings/BuildingWorldRuntime.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::building {
enum class BuildingAgentPhase:uint8_t {
    Idle,ApproachEntrance,OpenEntrance,Enter,Plan,Walk,Stairs,
    ApproachLift,CallLift,BoardLift,SelectFloor,Riding,ExitLift,FinishExit,
    Arrived,Blocked
};
struct BuildingAgentRoute {
    uint64_t sceneId=0;
    std::string room,goal,entrance,nextRoom;
    BuildingAgentPhase phase=BuildingAgentPhase::Idle;
    std::vector<Vec3> path;
    uint32_t waypoint=0,sequence=0,nextRequestTick=0;
    uint64_t navRevision=0;
    uint32_t feature=0,fromFloor=0,toFloor=0;
    Vec3 approach{},board{},exit{};
    std::string error;
};
inline void io(Archive& a,BuildingAgentRoute& r){
    a(r.sceneId,r.room,r.goal,r.entrance,r.nextRoom,r.phase,r.path,
      r.waypoint,r.sequence,r.nextRequestTick,r.navRevision,r.feature,
      r.fromFloor,r.toFloor,r.approach,r.board,r.exit,r.error);
    if(r.phase>BuildingAgentPhase::Blocked||r.path.size()>4096||r.waypoint>r.path.size()||
       r.room.size()>128||r.goal.size()>128||r.entrance.size()>128||r.nextRoom.size()>128||
       r.error.size()>256||r.fromFloor>=8||r.toFloor>=8)a.fail();
}
enum class BuildingAgentMoveKind:uint8_t { Ground,Stair,LiftThreshold };
enum class BuildingAgentMoveResult:uint8_t { Moving,Reached,Blocked };
struct BuildingAgentMotor {
    // Issue a target to the real ECS movement system or run its bounded
    // swept motor. Reached is checked against actual Transform position.
    std::function<BuildingAgentMoveResult(Simulation&,Entity,Vec3,BuildingAgentMoveKind)> move;
    std::function<void(Simulation&,Entity)> stop;
};

inline bool assignBuildingAgentGoal(World& ecs,Entity actor,uint64_t sceneId,
        std::string room,std::string goal,std::string entrance={}){
    if(!ecs.alive(actor)||!sceneId||goal.empty()||goal.size()>128||room.size()>128||
       entrance.size()>128||(room.empty()&&entrance.empty()))return false;
    const auto* rider=ecs.tryGetConst<BuildingLiftPassenger>(actor);
    if(rider&&rider->attached)return false; // never cancel/replace in transit
    BuildingAgentRoute next;next.sceneId=sceneId;next.room=std::move(room);
    next.goal=std::move(goal);next.entrance=std::move(entrance);
    if(auto* old=ecs.tryGet<BuildingAgentRoute>(actor)){
        next.sequence=old->sequence;*old=std::move(next);
        old->phase=old->entrance.empty()?BuildingAgentPhase::Plan:BuildingAgentPhase::ApproachEntrance;
    }else{
        next.phase=next.entrance.empty()?BuildingAgentPhase::Plan:BuildingAgentPhase::ApproachEntrance;
        ecs.add<BuildingAgentRoute>(actor,std::move(next));
    }
    return true;
}
inline bool buildingAgentAt(const World& ecs,Entity e,Vec3 target){
    const auto* t=ecs.tryGetConst<Transform>(e);
    if(!t)return false;
    const auto delta=t->pos-target;
    const Fixed tolerance=Fixed::fromRatio(1,5);
    return fxAbs(delta.x)<=tolerance&&fxAbs(delta.z)<=tolerance&&
           fxAbs(delta.y)<=Fixed::fromRatio(3,10);
}

inline void advanceBuildingAgent(Simulation& sim,BuildingWorldRuntime& runtime,
                   Entity e,BuildingAgentRoute& route,const BuildingAgentMotor& motor){
    auto& ecs=sim.world();
    const auto* owner=ecs.tryGetConst<Owner>(e);
    const auto* transform=ecs.tryGetConst<Transform>(e);
    if(!owner||!owner->player||!transform)return;
    const uint8_t player=owner->player;
    const auto blocked=[&](const std::string& reason){
        route.error=reason.substr(0,256);route.phase=BuildingAgentPhase::Blocked;
        motor.stop(sim,e);
    };
    if(route.phase==BuildingAgentPhase::Idle||route.phase==BuildingAgentPhase::Arrived||
       route.phase==BuildingAgentPhase::Blocked)return;
    if(!runtime.ready()||runtime.status(route.sceneId)!=BuildingWorldRuntime::Status::Active){
        motor.stop(sim,e);return;
    }
    const auto move=[&](Vec3 target,BuildingAgentMoveKind kind){
        if(buildingAgentAt(ecs,e,target))return true;
        const auto result=motor.move(sim,e,target,kind);
        if(result==BuildingAgentMoveResult::Blocked){blocked("Physical actor motor blocked");return false;}
        return result==BuildingAgentMoveResult::Reached&&buildingAgentAt(ecs,e,target);
    };
    const auto request=[&](std::optional<Command> command){
        if(!command||sim.tick()<route.nextRequestTick)return;
        if(route.sequence==UINT32_MAX){blocked("AI command sequence exhausted");return;}
        command->seq=++route.sequence;
        route.nextRequestTick=sim.tick()>UINT32_MAX-10?UINT32_MAX:sim.tick()+10;
        (void)runtime.enqueueAgentCommand(std::move(*command));
    };
    const auto liftRequest=[&](uint32_t floor,BuildingLiftCommandKind kind){
        request(runtime.makeLiftCommand(route.sceneId,route.feature,floor,e,player,kind));
    };
    const auto isRider=[&](){
        const auto* p=ecs.tryGetConst<BuildingLiftPassenger>(e);
        return p&&p->attached&&p->sceneId==route.sceneId&&p->liftFeature==route.feature;
    };
    const auto pathTo=[&](Vec3 point){
        route.path.clear();route.waypoint=0;
        const auto* t=ecs.tryGetConst<Transform>(e);
        return t&&runtime.agentPath(route.sceneId,t->pos,point,route.path,route.navRevision)&&
               route.path.size()<=4096&&!route.path.empty();
    };
    const auto follow=[&](BuildingAgentMoveKind kind){
        if(route.waypoint>=route.path.size())return true;
        if(move(route.path[route.waypoint],kind))++route.waypoint;
        return route.waypoint>=route.path.size();
    };
    switch(route.phase){
      case BuildingAgentPhase::ApproachEntrance:{
        BuildingWorldRuntime::AgentEntrance door;
        if(!runtime.agentEntrance(route.sceneId,route.entrance,door)){blocked("Unknown entry socket");break;}
        route.approach=door.outside;route.board=door.inside;route.room=door.room;
        if(move(route.approach,BuildingAgentMoveKind::Ground)){
            motor.stop(sim,e);route.phase=BuildingAgentPhase::OpenEntrance;
        }
        break;
      }
      case BuildingAgentPhase::OpenEntrance:
        motor.stop(sim,e);
        if(runtime.canTraverse(route.sceneId,route.entrance))route.phase=BuildingAgentPhase::Enter;
        else request(runtime.makeCommand(route.sceneId,route.entrance,e,player,true));
        break;
      case BuildingAgentPhase::Enter:
        if(!runtime.canTraverse(route.sceneId,route.entrance)){
            route.phase=BuildingAgentPhase::OpenEntrance;break;
        }
        if(move(route.board,BuildingAgentMoveKind::LiftThreshold))route.phase=BuildingAgentPhase::Plan;
        break;
      case BuildingAgentPhase::Plan:{
        if(route.room==route.goal){motor.stop(sim,e);route.phase=BuildingAgentPhase::Arrived;break;}
        auto plan=runtime.route(route.sceneId,route.room,route.goal);
        if(!plan.reachable||plan.steps.empty()){
            // Door/lift graph may be changing. Keep this goal and retry
            // from the actual current room rather than teleporting.
            motor.stop(sim,e);route.error=plan.error.substr(0,256);break;
        }
        const auto& edge=plan.steps.front();route.nextRoom=edge.toRoom;
        route.error.clear();route.waypoint=0;route.navRevision=plan.navRevision;
        if(edge.action==BuildingRouteAction::RequestLift){
            BuildingWorldRuntime::AgentLiftAccess access;
            if(!runtime.agentLiftAccess(route.sceneId,edge,access)){blocked("Invalid lift route");break;}
            route.feature=access.feature;route.fromFloor=access.fromFloor;route.toFloor=access.toFloor;
            route.approach=access.approach;route.board=access.board;route.exit=access.exit;
            if(!pathTo(route.approach)){blocked("No native path to lift landing");break;}
            route.phase=BuildingAgentPhase::ApproachLift;
        }else if(edge.action==BuildingRouteAction::ClimbStairs){
            if(edge.corridor.size()<2||!edge.from||!pathTo(*edge.from)){
                blocked("No path to stair landing");break;
            }
            if(route.path.size()+edge.corridor.size()>4096){blocked("Stair waypoint budget");break;}
            // Both approach and tread motion use real motor clearance.
            route.path.insert(route.path.end(),edge.corridor.begin(),edge.corridor.end());
            route.phase=BuildingAgentPhase::Stairs;
        }else{
            if(!edge.to||!pathTo(*edge.to)){blocked("No actual corridor to destination room");break;}
            route.phase=BuildingAgentPhase::Walk;
        }
        break;
      }
      case BuildingAgentPhase::Walk:
      case BuildingAgentPhase::ApproachLift:
        if(runtime.agentNavRevision(route.sceneId)!=route.navRevision){
            route.phase=BuildingAgentPhase::Plan;motor.stop(sim,e);break;
        }
        if(follow(BuildingAgentMoveKind::Ground)){
            if(route.phase==BuildingAgentPhase::Walk){route.room=route.nextRoom;route.phase=BuildingAgentPhase::Plan;}
            else{route.phase=BuildingAgentPhase::CallLift;motor.stop(sim,e);}
        }
        break;
      case BuildingAgentPhase::Stairs:
        // Authored tread path remains geometric; the real sweep rechecks
        // each step even when an unrelated lift changes the nav revision.
        if(follow(BuildingAgentMoveKind::Stair)){route.room=route.nextRoom;route.phase=BuildingAgentPhase::Plan;}
        break;
      case BuildingAgentPhase::CallLift:
        motor.stop(sim,e);
        if(runtime.canBoardLift(route.sceneId,route.feature,route.fromFloor))route.phase=BuildingAgentPhase::BoardLift;
        else liftRequest(route.fromFloor,BuildingLiftCommandKind::Call);
        break;
      case BuildingAgentPhase::BoardLift:
        if(isRider()){route.phase=BuildingAgentPhase::SelectFloor;motor.stop(sim,e);break;}
        if(!runtime.canBoardLift(route.sceneId,route.feature,route.fromFloor)){
            route.phase=BuildingAgentPhase::CallLift;break;
        }
        if(move(route.board,BuildingAgentMoveKind::LiftThreshold)){
            motor.stop(sim,e);liftRequest(route.fromFloor,BuildingLiftCommandKind::Board);
        }
        break;
      case BuildingAgentPhase::SelectFloor:
        if(!isRider()){blocked("Lost lift passenger binding");break;}
        motor.stop(sim,e);
        liftRequest(route.toFloor,BuildingLiftCommandKind::SelectFloor);
        route.phase=BuildingAgentPhase::Riding;break;
      case BuildingAgentPhase::Riding:
        motor.stop(sim,e);
        if(!isRider()){blocked("Lost lift passenger binding");break;}
        if(runtime.canBoardLift(route.sceneId,route.feature,route.toFloor))route.phase=BuildingAgentPhase::ExitLift;
        else if(runtime.canBoardLift(route.sceneId,route.feature,route.fromFloor))
            liftRequest(route.toFloor,BuildingLiftCommandKind::SelectFloor);
        break;
      case BuildingAgentPhase::ExitLift:
        if(!runtime.canBoardLift(route.sceneId,route.feature,route.toFloor)){
            motor.stop(sim,e);break;
        }
        if(move(route.exit,BuildingAgentMoveKind::LiftThreshold)){
            motor.stop(sim,e);route.phase=BuildingAgentPhase::FinishExit;
        }
        break;
      case BuildingAgentPhase::FinishExit:
        motor.stop(sim,e);
        if(isRider())liftRequest(route.toFloor,BuildingLiftCommandKind::Exit);
        else{route.room=route.nextRoom;route.phase=BuildingAgentPhase::Plan;}
        break;
      default:break;
    }
}

// Install once during GameModule::setup, before its existing locomotion
// and physical integration systems. The BuildingWorldRuntime fixed tick
// runs later, after the authoritative motion owner.
inline bool installBuildingAgentNavigation(Simulation& sim,
         std::weak_ptr<BuildingWorldRuntime> runtime,BuildingAgentMotor motor,
         size_t agentsPerTick=256){
    if(sim.setupComplete()||runtime.expired()||!motor.move||!motor.stop||!agentsPerTick)return false;
    sim.registerPersistentComponent<BuildingAgentRoute>("building.ai.route",1);
    sim.addSystem("building.ai.routes",[runtime,motor=std::move(motor),agentsPerTick](Simulation& current){
        auto active=runtime.lock();if(!active)return;
        auto& ecs=current.world();
        std::vector<Entity> actors;
        ecs.eachExistingConst<BuildingAgentRoute>([&](Entity e,const BuildingAgentRoute& route){
            if(ecs.alive(e)&&route.phase!=BuildingAgentPhase::Idle&&
               route.phase!=BuildingAgentPhase::Arrived&&route.phase!=BuildingAgentPhase::Blocked)
                actors.push_back(e);
        });
        std::sort(actors.begin(),actors.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        if(actors.empty())return;
        const size_t budget=std::min(agentsPerTick,actors.size());
        const size_t start=size_t((uint64_t(current.tick())*budget)%actors.size());
        for(size_t i=0;i<budget;++i){
            const auto actor=actors[(start+i)%actors.size()];
            if(auto* route=ecs.tryGet<BuildingAgentRoute>(actor))
                advanceBuildingAgent(current,*active,actor,*route,motor);
        }
    });
    return true;
}
}
